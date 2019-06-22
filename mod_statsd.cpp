/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 * Portions created by Seventh Signal Ltd. & Co. KG and its employees are Copyright (C)
 * Seventh Signal Ltd. & Co. KG, All Rights Reserverd.
 *
 * Contributor(s):
 * Joao Mesquita <jmesquita@sangoma.com>
 * Kinshuk Bairagi <me@kinshuk.in>
 *
 * mod_statsd.c -- Send metrics to statsd server
 *
 */
#include <switch.h>
#include <iostream>
#include <string>
#include <algorithm>
#include "statsd-client.hpp"

static struct {
	switch_memory_pool_t *pool;
	switch_mutex_t *mutex;
	int port;
	statsd_link* link;
	char* host;
	char* _namespace;
	int shutdown;
} globals;

typedef struct {
	char *chan_var_name;
	char *default_value;
	switch_bool_t quote;
} cdr_field_t;


// templated version of equals so it could work with both char and wchar_t
template<typename charT>
struct equals {
    equals( const std::locale& loc ) : loc_(loc) {}
    bool operator()(charT ch1, charT ch2) {
        return std::toupper(ch1, loc_) == std::toupper(ch2, loc_);
    }
private:
    const std::locale& loc_;
};

// find substring (case insensitive)
template<typename T>
int ci_find_substr( const T& str1, const T& str2, const std::locale& loc = std::locale() )
{
    typename T::const_iterator it = std::search( str1.begin(), str1.end(), 
        str2.begin(), str2.end(), equals<typename T::value_type>(loc) );
    if ( it != str1.end() ) return it - str1.begin();
    else return -1; // not found
}

#define SLEEP_INTERVAL 10*1000*1000 //1s


SWITCH_MODULE_LOAD_FUNCTION(mod_statsd_load);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_statsd_runtime);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_statsd_shutdown);
SWITCH_MODULE_DEFINITION(mod_statsd, mod_statsd_load, mod_statsd_shutdown, mod_statsd_runtime);


static switch_xml_config_item_t instructions[] = {
    SWITCH_CONFIG_ITEM("host", SWITCH_CONFIG_STRING, CONFIG_REQUIRED, &globals.host,
                        "127.0.0.1", NULL, NULL, "StatsD hostname"),
    SWITCH_CONFIG_ITEM("port", SWITCH_CONFIG_INT, CONFIG_REQUIRED, &globals.port,
                        (void *) 8125, NULL, NULL, NULL),
    SWITCH_CONFIG_ITEM("namespace", SWITCH_CONFIG_STRING, CONFIG_REQUIRED, &globals._namespace,
                        NULL, NULL, NULL, "StatsD namespace"),
    SWITCH_CONFIG_ITEM_END()
};


static switch_status_t load_config(switch_memory_pool_t *pool, switch_bool_t reload)
{
 	memset(&globals, 0, sizeof(globals));
 	globals.pool = pool;
	switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, globals.pool);

    if (switch_xml_config_parse_module_settings("statsd.conf", reload, instructions) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Could not open statsd.conf\n");

		const char* _host = "127.0.0.1";
		globals.host = (char*)malloc(strlen(_host)+1);
		strcpy( (char*)_host,globals.host);

		globals.port = 8125;
		return SWITCH_STATUS_FALSE;
    }  

    return SWITCH_STATUS_SUCCESS;
}

static int sql_count_callback(void *pArg, int argc, char **argv, char **columnNames)
{
	uint32_t *count = (uint32_t *) pArg;
	*count = atoi(argv[0]);
	return 0;
}

static std::string broken_rtp = "broken";

static switch_status_t statsd_cdr_reporting(switch_core_session_t *session)
{
	try {
		
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_memory_pool_t *pool = switch_core_session_get_pool(session);
		const char *uuid = NULL;
		std::string event_name;
		const char *tmp;
		
		int is_b = channel && switch_channel_get_originator_caller_profile(channel);

		if (!(uuid = switch_channel_get_variable(channel, "uuid"))) {
			uuid = switch_core_strdup(pool, switch_core_session_get_uuid(session));
		}

		// //hangup_cause
		// //hold_accum_seconds
		// //duration
		// //answersec
		// //sip_hangup_disposition
		// //quality_percentage

        if (is_b){
			statsd_count(globals.link, "call_finish_b" , 1, 1.0);

			tmp = switch_channel_get_variable(channel, "duration");
			statsd_gauge(globals.link, "call_duration_b" , atoi(tmp));

			tmp = switch_channel_get_variable(channel, "answersec");
			statsd_gauge(globals.link, "call_answer_seconds_b" , atoi(tmp));

			tmp = switch_channel_get_variable(channel, "hold_accum_seconds");
			statsd_gauge(globals.link, "call_hold_seconds_b" , atoi(tmp));

        } else {
			statsd_count(globals.link, "call_finish" , 1, 1.0);

			tmp = switch_channel_get_variable(channel, "duration");
			statsd_gauge(globals.link, "call_duration" , atoi(tmp));

			tmp = switch_channel_get_variable(channel, "answersec");
			statsd_gauge(globals.link, "call_answer_seconds" , atoi(tmp));

			tmp = switch_channel_get_variable(channel, "hold_accum_seconds");
			statsd_gauge(globals.link, "call_hold_seconds" , atoi(tmp));
        }

		tmp = switch_channel_get_variable(channel, "hangup_cause");
		event_name = "hangup_cause."  + (std::string) tmp;
		statsd_count(globals.link, event_name , 1, 1.0);

		tmp = switch_channel_get_variable(channel, "sip_reason");
		if(tmp != NULL){
        	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "HANGUP %s sip_reason %s\n",uuid, tmp);
			if(ci_find_substr((std::string) tmp, broken_rtp) != -1){
		        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "HANGUP %s due to RTP Issue\n",uuid);
				statsd_count(globals.link, "hangup_info.RTP_BROKEN" , 1, 1.0);
			}
		}
		
		return SWITCH_STATUS_SUCCESS;

	} catch(...) { 
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error running statsd_cdr_reporting\n");
        return SWITCH_STATUS_SUCCESS;
    }
}


static switch_status_t statsd_call_reporting(switch_core_session_t *session)
{
	try {

		switch_channel_t *channel = switch_core_session_get_channel(session);
		int is_b = channel && switch_channel_get_originator_caller_profile(channel);

		if (is_b){
			statsd_count(globals.link, "call_init_b" , 1, 1.0);
        } else {
			statsd_count(globals.link, "call_init" , 1, 1.0);
        }
		return SWITCH_STATUS_SUCCESS;

	} catch(...) { 
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error running statsd_call_reporting\n");
        return SWITCH_STATUS_SUCCESS;
    }
}


switch_state_handler_table_t statsd_state_handlers = {
	/*.on_init */ statsd_call_reporting,
	/*.on_routing */ NULL,
	/*.on_execute */ NULL,
	/*.on_hangup */ NULL,
	/*.on_exchange_media */ NULL,
	/*.on_soft_execute */ NULL,
	/*.on_consume_media */ NULL,
	/*.on_hibernate */ NULL,
	/*.on_reset */ NULL,
	/*.on_park */ NULL,
	/*.on_reporting */ statsd_cdr_reporting,
	/*.on_destroy */ NULL
};



/**
 * Polls for all metrics
 */
SWITCH_MODULE_RUNTIME_FUNCTION(mod_statsd_runtime)
{
	uint32_t int_val;
	switch_cache_db_handle_t *dbh;
	char sql[1024] = "";

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Starting Poll for metrics\n");

	while(globals.shutdown == 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Running Poll for metrics\n");

		switch_mutex_lock(globals.mutex);

		statsd_gauge(globals.link, "sessions_since_startup", switch_core_session_id() - 1);
		statsd_gauge(globals.link, "sessions_count", switch_core_session_count());
		switch_core_session_ctl(SCSC_SESSIONS_PEAK, &int_val);
		statsd_gauge(globals.link, "sessions_count_peak", int_val);
		switch_core_session_ctl(SCSC_LAST_SPS, &int_val);
		statsd_gauge(globals.link, "sessions_per_second", int_val);
		switch_core_session_ctl(SCSC_SPS_PEAK, &int_val);
		statsd_gauge(globals.link, "sessions_per_second_peak", int_val);
		switch_core_session_ctl(SCSC_SESSIONS_PEAK_FIVEMIN, &int_val);
		statsd_gauge(globals.link, "sessions_per_second_5min", int_val);


		if (switch_core_db_handle(&dbh) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "No database to output calls or channels.\n");
		} else {
			sprintf(sql, "SELECT COUNT(*) FROM basic_calls WHERE hostname='%s'", switch_core_get_switchname());
			switch_cache_db_execute_sql_callback(dbh, sql, sql_count_callback, &int_val, NULL);
			statsd_gauge(globals.link, "call_count", int_val);

			sprintf(sql, "select count(*) from channels where hostname='%s'", switch_core_get_switchname());
			switch_cache_db_execute_sql_callback(dbh, sql, sql_count_callback, &int_val, NULL);
			statsd_gauge(globals.link, "channel_count", int_val);

			sprintf(sql, "select count(*) from registrations where hostname='%s'", switch_core_get_switchname());
			switch_cache_db_execute_sql_callback(dbh, sql, sql_count_callback, &int_val, NULL);
			statsd_gauge(globals.link, "registration_count", int_val);
			switch_cache_db_release_db_handle(&dbh);
		}

		switch_mutex_unlock(globals.mutex);
		switch_sleep(SLEEP_INTERVAL); 
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Runtime thread is done\n");
	return SWITCH_STATUS_TERM;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_statsd_load)
{

	try {

	    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "mod_statsd Initialising... \n");
	  
		//switch_management_interface_t *management_interface;

		switch_core_add_state_handler(&statsd_state_handlers);

		*module_interface = switch_loadable_module_create_module_interface(pool, modname);
		//management_interface = (switch_management_interface_t *) switch_loadable_module_create_interface(*module_interface, SWITCH_MANAGEMENT_INTERFACE);
		//management_interface->relative_oid = "2000";

		load_config(pool, SWITCH_FALSE);

		if (zstr(globals._namespace)) {
			globals.link = statsd_init(globals.host, globals.port);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending stats to %s:%d\n", globals.host, globals.port);
		} else {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
					"Setting up stats to %s:%d with namespace %s\n", globals.host, globals.port, globals._namespace);
			auto socket = statsd_init_with_namespace(globals.host, globals.port, globals._namespace);
			globals.link  = socket;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
					"Sending stats to %s:%d with namespace %s\n", globals.host, globals.port, globals._namespace);
		}

	    return SWITCH_STATUS_SUCCESS;


	} catch(...) { // Exceptions must not propogate to C caller
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error loading mod_statsd module\n");
        return SWITCH_STATUS_GENERR;
    }
            

}


SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_statsd_shutdown)
{
	switch_mutex_lock(globals.mutex);
	statsd_finalize(globals.link);
	globals.shutdown = 1;
	switch_mutex_unlock(globals.mutex);

	switch_mutex_destroy(globals.mutex);

	switch_xml_config_cleanup(instructions);

	switch_core_remove_state_handler(&statsd_state_handlers);

	return SWITCH_STATUS_SUCCESS;
}



/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
