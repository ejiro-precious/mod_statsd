#include <switch.h>
#include <iostream>
#include "statsd-client.hpp"

#define MAX_MSG_LEN 1024

statsd_link* statsd_init_with_namespace(const char*  host, int port, const char* ns_)
{
    size_t len;
    statsd_link *temp = NULL;

    if (!host == 0 || !port || !ns_){
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "statsd_init_with_namespace() assert failed.\n");
        return NULL;
    }

    len = strlen(ns_);
    temp = statsd_init(host, port);

    if ( (temp->ns = (char*) malloc(len + 2)) == NULL ) {
        perror("malloc");
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "malloc() failed\n");
        return NULL;
    }
    strcpy(temp->ns, ns_);
    temp->ns[len++] = '.';
    temp->ns[len] = 0;

    return temp;
}

statsd_link* statsd_init(const char* host, int port)
{
    statsd_link *temp;
    struct addrinfo *result = NULL;
    struct addrinfo hints;
    int error;

    if (!host || !port){
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "statsd_init() assert failed.\n");
        return NULL;
    }

    temp = (statsd_link*) calloc(1, sizeof(statsd_link));
    if (!temp) {
        fprintf(stderr, "calloc() failed");
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "calloc() failed\n");
        goto err;
    }

    if ((temp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("socket");
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "socket() errored\n");
        goto err;
    }

    memset(&temp->server, 0, sizeof(temp->server));
    temp->server.sin_family = AF_INET;
    temp->server.sin_port = htons(port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ( (error = getaddrinfo(host, NULL, &hints, &result)) ) {
        fprintf(stderr, "%s\n", gai_strerror(error));
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "getaddrinfo() failed %s\n", gai_strerror(error));
        goto err;
    }
    memcpy(&(temp->server.sin_addr), &((struct sockaddr_in*)result->ai_addr)->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);

    srandom(time(NULL));
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "statsd_init() success.\n");
    return temp;

err:
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "statsd_init() errored.\n");

    if (temp) {
        free(temp);
    }

    return NULL;
}

void statsd_finalize(statsd_link *link)
{
    if (!link) return;

    // close socket
    if (link->sock != -1) {
        close(link->sock);
        link->sock = -1;
    }

    // freeing ns
    if (link->ns) {
        free(link->ns);
        link->ns = NULL;
    }

    // free whole link
    free(link);
}

/* will change the original string */
static void cleanup(std::string stat)
{
    for(unsigned int i = 0; i < stat.size(); i++) {
        char x = stat.at(i);
        if (x == ':' || x == '|' || x == '@') {
            stat.at(i) = '_';
        }

    }
}

static int should_send(float sample_rate)
{
    float p;
    if (sample_rate < 1.0) {
        p = ((float)random() / RAND_MAX);
        return sample_rate > p;
    } else {
        return 1;
    }
}

int statsd_send(statsd_link *link, const char *message)
{
    if (!link) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "statsd_send() failed as socket unavailable.\n");
        return -2;
    }
    int slen = sizeof(link->server);

    if (sendto(link->sock, message, strlen(message), 0, (struct sockaddr *) &link->server, slen) == -1) {
        perror("sendto");
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "statsd_send sendto() errored.\n");
        return -1;
    }
    return 0;
}

static int send_stat(statsd_link *link, std::string stat, size_t value, const char *type, float sample_rate)
{
    char message[MAX_MSG_LEN];
    if (!should_send(sample_rate)) {
        return 0;
    }

    statsd_prepare(link, stat, value, type, sample_rate, message, MAX_MSG_LEN, 0);

    return statsd_send(link, message);
}

void statsd_prepare(statsd_link *link, std::string stat, size_t value, const char *type, float sample_rate, char *message, size_t buflen, int lf)
{
    if (!link) return;

    cleanup(stat);
    if (sample_rate == 1.0) {
        snprintf(message, buflen, "%s%s:%zd|%s%s", link->ns ? link->ns : "", stat.c_str(), value, type, lf ? "\n" : "");
    } else {
        snprintf(message, buflen, "%s%s:%zd|%s|@%.2f%s", link->ns ? link->ns : "", stat.c_str(), value, type, sample_rate, lf ? "\n" : "");
    }
}

/* public interface */
int statsd_count(statsd_link *link, std::string stat, size_t value, float sample_rate)
{
    return send_stat(link, stat, value, "c", sample_rate);
}

int statsd_dec(statsd_link *link, std::string stat, float sample_rate)
{
    return statsd_count(link, stat, -1, sample_rate);
}

int statsd_inc(statsd_link *link, std::string stat, float sample_rate)
{
    return statsd_count(link, stat, 1, sample_rate);
}

int statsd_gauge(statsd_link *link, std::string stat, size_t value)
{
    return send_stat(link, stat, value, "g", 1.0);
}

int statsd_timing(statsd_link *link, std::string stat, size_t ms)
{
    return send_stat(link, stat, ms, "ms", 1.0);
}
