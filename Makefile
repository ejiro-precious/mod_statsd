MODNAME = mod_statsd.so
MODOBJ = mod_statsd.o statsd-client.o
MODCFLAGS = -Wall -Werror 
MODLDFLAGS = -lz -lpthread -lrt 

CXX = /usr/bin/g++
CFLAGS = -fPIC -g -ggdb -I/usr/include  `pkg-config --cflags freeswitch` $(MODCFLAGS) -std=c++0x
LDFLAGS = `pkg-config --libs freeswitch` $(MODLDFLAGS) 

.PHONY: all
all: $(MODNAME)

$(MODNAME): $(MODOBJ)
	@$(CXX) -shared -o $@ $(MODOBJ) $(LDFLAGS)

.cpp.o: $<
	@$(CXX) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -f $(MODNAME) $(MODOBJ)

.PHONY: install
install: $(MODNAME)
	install -d $(DESTDIR)/usr/lib/freeswitch/mod
	install $(MODNAME) $(DESTDIR)/usr/lib/freeswitch/mod
	install -d $(DESTDIR)/etc/freeswitch/autoload_configs
	install statsd.conf.xml $(DESTDIR)/etc/freeswitch/autoload_configs/

.PHONY: release
release: $(MODNAME)
	distribution/make-deb.sh