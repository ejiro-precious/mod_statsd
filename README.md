# FreeSWITCH StatsD module

Install this plugin to publish freeswitch metrics to a statsd endpoint. To enable just configure the `statsd.conf.xml` 

```xml
<configuration name="statsd.conf" description="StatsD Configuration">
	<settings>
		<param name="host" value="127.0.0.1"/>
		<param name="port" value="8125"/>
		<param name="namespace" value="freeswitch"/>
	</settings>
 </configuration>
```
and enable autoloading of the module by adding the following entry in `modules.conf.xml`

```xml
 <load module="mod_statsd"/>
```

