# fty-sensor-env

Agent fty-sensor-env reads metrics from T&H sensors and GPI sensors.

## How to build

To build fty-sensor-env project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```

## How to run

To run fty-sensor-env project:

* from within the source tree, run:

```bash
./src/fty-sensor-env
```

For the other options available, refer to the manual page of fty-sensor-env

* from an installed base, using systemd, run:

```bash
systemctl start fty-sensor-env
```

### Configuration file

Agent doesn't have configuration file.

## Architecture

### Overview

fty-sensor-env has 1 actor:

* fty-sensor-env-server: main actor

It also has one built-in timer, which runs each 5 seconds and reads data from sensors.

## Protocols

### Published metrics

Agent publishes metrics on \_METRICS\_SENSOR stream:

```bash
stream=_METRICS_SENSOR
sender=fty-sensor-env
subject=temperature./dev/ttyS9@rackcontroller-0
D: 18-01-24 11:15:07 FTY_PROTO_METRIC:
D: 18-01-24 11:15:07     aux=
D: 18-01-24 11:15:07         sname=sensor-70
D: 18-01-24 11:15:07         port=9
D: 18-01-24 11:15:07     time=1516792507
D: 18-01-24 11:15:07     ttl=300
D: 18-01-24 11:15:07     type='temperature./dev/ttyS9'
D: 18-01-24 11:15:07     name='rackcontroller-0'
D: 18-01-24 11:15:07     value='23.00'
D: 18-01-24 11:15:07     unit='C'
```

### Published alerts

Agent doesn't publish any alerts.

### Mailbox requests

Agent doesn't receive any mailbox requests.

### Stream subscriptions

Agent is subscribed to ASSETS stream and processes messages about T&H and GPI sensors.

On CREATE, agent creates ne sensor in its cache.   
On UPDATE, it updates the cache.  
On DELETE, it deletes the sensor from the cache.  
