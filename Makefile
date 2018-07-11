MODNAME = mod_statsd.so
MODOBJ = mod_statsd.o statsd-client.o
MODCFLAGS = -Wall -Werror 
MODLDFLAGS = -lz -lpthread -lrt 

C = gcc
CFLAGS = -fPIC -g -ggdb -I/usr/include  `pkg-config --cflags freeswitch` $(MODCFLAGS)
LDFLAGS = `pkg-config --libs freeswitch` $(MODLDFLAGS) 

.PHONY: all
all: $(MODNAME)

$(MODNAME): $(MODOBJ)
	@$(C) -shared -o $@ $(MODOBJ) $(LDFLAGS)

.c.o: $<
	@$(C) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -f $(MODNAME) $(MODOBJ)

.PHONY: install
install: $(MODNAME)
	install -d $(DESTDIR)/usr/lib/freeswitch/mod
	install $(MODNAME) $(DESTDIR)/usr/lib/freeswitch/mod
	install -d $(DESTDIR)/etc/freeswitch/autoload_configs
	install statsd.conf.xml $(DESTDIR)/etc/freeswitch/autoload_configs/