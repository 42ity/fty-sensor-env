/*  =========================================================================
    fty_sensor_env_server - Grab temperature and humidity data from sensors attached to the box

    Copyright (C) 2014 - 2018 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/*
@header
    fty_sensor_env_server - Grab temperature and humidity data from sensors attached to the box
@discuss
@end
*/

#include "fty_sensor_env_classes.h"

//  Structure of our class

// volatile global variable
volatile char s_interrupted = 0;

// for testing
int testing = 0;
#ifdef __GNUC__
    #define unlikely(x) __builtin_expect(0 != x, 0)
#else
    #define unlikely(x) (0 != x)
#endif
#define open_device(...) \
    (unlikely(testing) ? (testing-1) : open_device(__VA_ARGS__))
#define device_connected(...) \
    (unlikely(testing) ? (testing-1) : device_connected(__VA_ARGS__))
#define reset_device(...) \
    unlikely(testing) ? (void)0 : reset_device(__VA_ARGS__)
#define get_th_data(...) \
    (unlikely(testing) ? 1 : get_th_data(__VA_ARGS__))
#define compensate_temp(...) \
    unlikely(testing) ? (void)0 : compensate_temp(__VA_ARGS__)
#define compensate_humidity(...) \
    unlikely(testing) ? (void)0 : compensate_humidity(__VA_ARGS__)
#define read_gpi(...) \
    (unlikely(testing) ? 1 : read_gpi(__VA_ARGS__))

#define PORTMAP_LENGTH 12
const char *portmapping[2][PORTMAP_LENGTH] = {
        {
            // standard serial ports which can be used for T&H too
            // don't need/use the portmapping
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            // Real T&H dedicated serial ports
            "/dev/ttySTH1", "/dev/ttySTH2", "/dev/ttySTH3", "/dev/ttySTH4" },
        {
            // standard serial ports which can be used for T&H too
            "/dev/ttyS1",   "/dev/ttyS2",   "/dev/ttyS3",   "/dev/ttyS4",
            "/dev/ttyS5",   "/dev/ttyS6",   "/dev/ttyS7",   "/dev/ttyS8",
            // Real T&H dedicated serial ports
            "/dev/ttyS9",   "/dev/ttyS10",  "/dev/ttyS11",  "/dev/ttyS12"
        }
};

typedef struct _c_item {
    int32_t T;
    int32_t H;
} c_item_t;

typedef struct _ext_sensor {
    char    *iname;
    char    *rack_iname;
    char    *port;
    char    temperature;
    char    humidity;
    zhash_t *gpi;
    char    valid;
} external_sensor_t;

//  Structure of our class

struct _fty_sensor_env_server_t {
    mlm_client_t    *mlm;
    zhash_t         *portmap;
    zhash_t         *gpi_env_pairing;
    zlist_t         *sensors;
};


//  --------------------------------------------------------------------------
//  Properly free a sensor

void
free_sensor(void *sensor) {
    if (NULL == sensor) return;
    if (((external_sensor_t *)sensor)->iname) free(((external_sensor_t *)sensor)->iname);
    if (((external_sensor_t *)sensor)->rack_iname) free(((external_sensor_t *)sensor)->rack_iname);
    if (((external_sensor_t *)sensor)->port) free(((external_sensor_t *)sensor)->port);
    zhash_destroy(&(((external_sensor_t *)sensor)->gpi));
    ((external_sensor_t *)sensor)->valid = DELETED;
    free(sensor);
}


//  --------------------------------------------------------------------------
//  Create a new fty_sensor_env_server

fty_sensor_env_server_t *
fty_sensor_env_server_new (void)
{
    fty_sensor_env_server_t *self = (fty_sensor_env_server_t *) zmalloc (sizeof (fty_sensor_env_server_t));
    assert (self);
    //  Initialize class properties here
    self->mlm = mlm_client_new ();
    if (!(self->mlm)) {
        log_error ("mlm_client_new ) failed");
        return NULL;
    }
    self->portmap = zhash_new();
    if (!(self->portmap)) {
        log_error ("portmap zhash_new() failed");
        return NULL;
    }
    zhash_autofree(self->portmap);
    self->gpi_env_pairing = zhash_new();
    if (!(self->gpi_env_pairing)) {
        log_error ("gpi_env_pairing zhash_new() failed");
        return NULL;
    }
    zhash_autofree(self->gpi_env_pairing);
    self->sensors = zlist_new ();
    if (!(self->sensors)) {
        log_error ("sensors zlist_new() failed");
        return NULL;
    }
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the fty_sensor_env_server

void
fty_sensor_env_server_destroy (fty_sensor_env_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        fty_sensor_env_server_t *self = *self_p;
        //  Free class properties here
        mlm_client_destroy (&(self->mlm));
        zlist_purge(self->sensors);
        zlist_destroy (&(self->sensors));
        zhash_destroy (&(self->portmap));
        zhash_destroy (&(self->gpi_env_pairing));
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Free function for any mallocated variables

static void
freefn(void *var)
{
    if (var) free(var);
}


//  --------------------------------------------------------------------------
//  Search for a sensor in sensors list

external_sensor_t *
search_sensor(zlist_t *sensor_list, const char *iname) {
    external_sensor_t *sensor = (external_sensor_t *) zlist_first(sensor_list);
    while (sensor) {
        if (0 == strcmp(sensor->iname, iname)) {
            break;
        }
        sensor = (external_sensor_t *) zlist_next(sensor_list);
    }
    return sensor;
}


//  --------------------------------------------------------------------------
//  Create a new sensor for sensors list

external_sensor_t *
create_sensor(const char *name, const char temperature, const char humidity, const char valid) {
    external_sensor_t *sensor = (external_sensor_t *) malloc(sizeof(external_sensor_t));
    if (!sensor) return NULL;
    sensor->iname = strdup(name);
    sensor->rack_iname = NULL;
    sensor->port = NULL;
    sensor->temperature = temperature;
    sensor->humidity = humidity;
    sensor->gpi = zhash_new();
    zhash_autofree(sensor->gpi);
    sensor->valid = valid;
    return sensor;
}


//  --------------------------------------------------------------------------
//  Measure sensors connected to serial port

fty_proto_t*
get_measurement (const char what, const char *port_file) {
    if (DISABLED == what) {
        return NULL;
    }
    fty_proto_t* ret = fty_proto_new (FTY_PROTO_METRIC);
    c_item_t data = { 0, 0 };

    int fd = open_device(port_file);
    if(!device_connected(fd)) {
        if(fd > 0)
            close(fd);
        log_debug("No sensor attached to %s", port_file);
        fty_proto_destroy (&ret);
        return NULL;
    }
    else {
        reset_device(fd);
        if (TEMPERATURE == what) {
            data.T = get_th_data(fd, MEASURE_TEMP);
            compensate_temp(data.T, &data.T);
            log_debug("Got data from sensor '%s' - T = %" PRId32 ".%02" PRId32 " C", port_file, data.T/100, data.T%100);

            fty_proto_set_value (ret, "%.2f", data.T / (float) 100);
            fty_proto_set_unit (ret, "%s", "C");

            log_debug ("Returning T = %s C", fty_proto_value (ret));
        } else if (HUMIDITY == what) {
            data.T = get_th_data(fd, MEASURE_TEMP);
            data.H = get_th_data(fd, MEASURE_HUMI);
            compensate_humidity(data.H, data.T, &data.H);
            log_debug("Got data from sensor '%s' - H = %" PRId32 ".%02" PRId32 " %%", port_file, data.H/100, data.H%100);

            fty_proto_set_value (ret, "%.2f", data.H / (float) 100);
            fty_proto_set_unit (ret, "%s", "%");

            log_debug ("Returning H = %s %%", fty_proto_value (ret));
        } else {
            // port number expected
            int gpi = read_gpi(fd, what);
            if (0 == gpi) {
                fty_proto_set_value (ret, "opened");
            } else if (1 == gpi) {
                fty_proto_set_value (ret, "closed");
            } else {
                fty_proto_set_value (ret, "invalid");
            }
            fty_proto_set_unit (ret, "%s", "");

            log_debug ("Returning S = %s", fty_proto_value (ret));
        }
        close(fd);
    }
    return ret;
}


//  --------------------------------------------------------------------------
//  Send message containing sensor values

static int
send_message(mlm_client_t *client, fty_proto_t *msg, const external_sensor_t *sensor,
        const char *type, const char *sname, const char *ext_port) {
    if (NULL == client || NULL == msg || NULL == sensor || NULL == type || NULL == sname) {
        return 1;
    }
    fty_proto_set_ttl(msg, TIME_TO_LIVE);
    fty_proto_set_name(msg, "%s", sensor->rack_iname);
    fty_proto_set_time(msg, time (NULL));
    fty_proto_set_type(msg, "%s", type);
    zhash_t *aux = zhash_new();
    zhash_autofree (aux);
    if (ext_port) {
        zhash_insert (aux, "ext-port", (char *)ext_port);
    }
    zhash_insert(aux, "sname", (char *)sname);
    zhash_insert(aux, "port", sensor->port);
    fty_proto_set_aux(msg, &aux);
    char *subject = zsys_sprintf("%s@%s", type, sensor->rack_iname);
    zmsg_t *to_send = fty_proto_encode (&msg);
    int rv = mlm_client_send (client, subject, &to_send);
    zstr_free(&subject);
    fty_proto_destroy (&msg);
    if (rv != 0) {
        log_error ("mlm_client_send (subject = '%s') failed", subject);
        return 1;
    }
    return 0;
}


//  --------------------------------------------------------------------------
//  Attempt to read values from sensors and publish results

static void
read_sensors (fty_sensor_env_server_t *self)
{
    assert (self->mlm);
    external_sensor_t *sensor = (external_sensor_t *) zlist_first(self->sensors);
    while (NULL != sensor) {
        if (INVALID == sensor->valid) {
            // nothing to be done for INVALID sensors
            sensor = (external_sensor_t *) zlist_next(self->sensors);
            continue;
        }
        const char *port_file = (char *) zhash_lookup(self->portmap, sensor->port);
        fty_proto_t* msg = NULL;
        if (VALID == sensor->valid) { // we measure only active sensors for T&H
            log_debug ("Measuring '%s%s'", TH, sensor->port);
            log_debug ("Reading from '%s'", port_file);
            if (s_interrupted) {
                break;
            }
            fty_proto_t* msg = get_measurement(TEMPERATURE, port_file);
            if (msg) {
                char *type = zsys_sprintf("%s.%s", TEMPERATURE_STR, port_file);
                send_message(self->mlm, msg, sensor, type, sensor->iname, NULL);
                zstr_free(&type);
            }
            if (s_interrupted) {
                break;
            }
            msg = get_measurement(HUMIDITY, port_file);
            if (msg) {
                char *type = zsys_sprintf("%s.%s", HUMIDITY_STR, port_file);
                send_message(self->mlm, msg, sensor, type, sensor->iname, NULL);
                zstr_free(&type);
            }
        }
        // GPI sensors are checked regardless of their master state (both VALID and INACTIVE)
        char *sensor_gpi_port = (char *) zhash_first(sensor->gpi);
        while (sensor_gpi_port) {
            int sensor_gpi_port_num = atoi(sensor_gpi_port);
            if (s_interrupted) {
                break;
            }
            msg = get_measurement(sensor_gpi_port_num, port_file);
            if (msg) {
                char *type = zsys_sprintf("%s%s.%s", STATUSGPI_STR, sensor_gpi_port, port_file);
                send_message(self->mlm, msg, sensor, type, (char *) zhash_cursor(sensor->gpi), sensor_gpi_port);
                zstr_free(&type);
            }
            sensor_gpi_port = (char *) zhash_next(sensor->gpi);
        }
        if (s_interrupted) {
            break;
        }
        sensor = (external_sensor_t *) zlist_next(self->sensors);
    }
}


//  --------------------------------------------------------------------------
//  Read ASSET data containing sensor or sensorgpio and add them to sensors list

int
handle_proto_sensor(fty_sensor_env_server_t *self, zmsg_t *message) {
    fty_proto_t *asset = fty_proto_decode (&message);
    if (!asset || fty_proto_id (asset) != FTY_PROTO_ASSET) {
        fty_proto_destroy (&asset);
        log_warning ("fty_proto_decode () failed OR received message not FTY_PROTO_ASSET");
        return -1;
    }

    const char *operation = fty_proto_operation (asset);
    const char *type = fty_proto_aux_string (asset, "type", "");
    const char *subtype = fty_proto_aux_string (asset, "subtype", "");
    const char *name = fty_proto_name (asset);
    log_info ("Received an asset message, operation = '%s', name = '%s', type = '%s', subtype = '%s'",
            operation, name, type, subtype);

    if (0 == strcmp(type, "device")) {
        if (0 == fty_proto_aux_number (asset, "parent", 0)) {
            log_warning ("There are no parents available");
        }
        const char *port = fty_proto_ext_string(asset, FTY_PROTO_ASSET_EXT_PORT, NULL);
        const char *parent1 = fty_proto_aux_string(asset, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, NULL);
        if (0 == strncmp(subtype, "sensorgpio", strlen("sensorgpio"))) {
            external_sensor_t *sensor = NULL;
            if (parent1) {
                sensor = (external_sensor_t *)search_sensor(self->sensors, parent1);
            } else {
                const char *known_parent = (char *) zhash_lookup(self->gpi_env_pairing, name);
                if (known_parent) {
                    sensor = (external_sensor_t *)search_sensor(self->sensors, known_parent);
                } else {
                    log_error("Unable to detect previous parent and none provided");
                    fty_proto_destroy (&asset);
                    return 1;
                }
            }
            if (streq (operation, FTY_PROTO_ASSET_OP_DELETE) ||
                    streq (operation, FTY_PROTO_ASSET_OP_RETIRE) ||
                    !streq(fty_proto_aux_string (asset, FTY_PROTO_ASSET_STATUS, "active"), "active")) {
                // simple delete
                if (sensor) {
                    zhash_delete(sensor->gpi, name);
                    if ((0 == zhash_size(sensor->gpi)) && (VALID != sensor->valid)) {
                        zlist_remove(self->sensors, sensor);
                    }
                    zhash_delete(self->gpi_env_pairing, name);
                }
            } else if (streq (operation, FTY_PROTO_ASSET_OP_CREATE) ||
                    streq (operation, FTY_PROTO_ASSET_OP_UPDATE)) {
                if (!port || !parent1) {
                    log_error ("Attempted to create sensorgpio, but missing %s in asset message",
                            port ? parent1 ? "" : "parent1" : parent1 ? "port" : "port and parent1");
                    fty_proto_destroy (&asset);
                    return 1;
                }
                // ignore sensors that don't have parent2 rackcontroller-0 - parent1 must be some EMP sensor
                const char *parent2 = fty_proto_aux_string(asset, "parent_name.2", NULL);
                if (!parent2) {
                    log_error ("Attempted to create sensorgpio, but missing parent2 in asset message");
                    fty_proto_destroy (&asset);
                    return 1;
                }
                if (! streq (parent2, "rackcontroller-0")) {
                    log_debug ("parent2 not rackcontroller-0, skipping this one");
                    fty_proto_destroy (&asset);
                    return 1;
                }
                if (sensor) {
                    // delete gpi sensor if there was one attached to different env one
                    const char *previous_parent = (char *) zhash_lookup(self->gpi_env_pairing, name);
                    if (previous_parent && !streq(previous_parent, parent1)) {
                        external_sensor_t *previous_parent_sensor = (external_sensor_t *)search_sensor(self->sensors, previous_parent);
                        if (previous_parent_sensor && previous_parent_sensor != sensor) {
                            zhash_delete(previous_parent_sensor->gpi, name);
                            if ((0 == zhash_size(previous_parent_sensor->gpi)) && (VALID != previous_parent_sensor->valid)) {
                                zlist_remove(self->sensors, previous_parent_sensor);
                            }
                        }
                        zhash_update(self->gpi_env_pairing, name, (char *)parent1);
                    }
                    if (!previous_parent) {
                        zhash_update(self->gpi_env_pairing, name, (char *)parent1);
                    }
                    // add GPI sensor to Sensor
                    zhash_update(sensor->gpi, name, (char *)port);
                    zhash_freefn(sensor->gpi, name, freefn);
                } else {
                    // delete gpi sensor if there was one attached to different env one
                    const char *previous_parent = (char *) zhash_lookup(self->gpi_env_pairing, name);
                    if (previous_parent && !streq(previous_parent, parent1)) {
                        external_sensor_t *previous_parent_sensor = (external_sensor_t *)search_sensor(self->sensors, previous_parent);
                        if (previous_parent_sensor) {
                            zhash_delete(previous_parent_sensor->gpi, name);
                            if ((0 == zhash_size(previous_parent_sensor->gpi)) && (VALID != previous_parent_sensor->valid)) {
                                zlist_remove(self->sensors, previous_parent_sensor);
                            }
                        }
                        zhash_update(self->gpi_env_pairing, name, (char *)parent1);
                    }
                    if (!previous_parent) {
                        zhash_update(self->gpi_env_pairing, name, (char *)parent1);
                    }
                    // create Sensor as it seems we don't know it yet, and mark if for update
                    sensor = create_sensor(parent1, DISABLED, DISABLED, INVALID);
                    zhash_update(sensor->gpi, name, (char *)port);
                    zhash_freefn(sensor->gpi, name, freefn);
                    zlist_append(self->sensors, sensor);
                    zlist_freefn(self->sensors, sensor, free_sensor, true);
                }
            }
        }
        else if (0 == strncmp(subtype, "sensor", strlen("sensor"))) {
            external_sensor_t *sensor = (external_sensor_t *)search_sensor(self->sensors, name);
            if (streq (operation, FTY_PROTO_ASSET_OP_DELETE) ||
                    streq (operation, FTY_PROTO_ASSET_OP_RETIRE) ||
                    !streq(fty_proto_aux_string (asset, FTY_PROTO_ASSET_STATUS, "active"), "active")) {
                // simple delete with deallocation
                if (sensor) {
                    if (0 == zhash_size(sensor->gpi)) {
                        // sensor is valid and has no gpio sensors attached
                        zlist_remove(self->sensors, sensor);
                    } else {
                        sensor->valid = INACTIVE;
                    }
                }
            } else if (streq (operation, FTY_PROTO_ASSET_OP_CREATE) ||
                    streq (operation, FTY_PROTO_ASSET_OP_UPDATE)) {
                if (!port || !parent1) {
                    log_error ("Attempted to create sensorgpio, but missing %s in asset message",
                            port ? parent1 ? "" : "parent1" : parent1 ? "port" : "port and parent1");
                    fty_proto_destroy (&asset);
                    return 1;
                }
                // ignore sensors that don't have parent1 rackcontroller-0
                if (! streq (parent1, "rackcontroller-0")) {
                    log_debug ("parent1 not rackcontroller-0, skipping this one");
                    fty_proto_destroy (&asset);
                    return 1;
                }
                if (sensor) {
                    // update sensor
                    sensor->valid = VALID;
                    if (!(sensor->rack_iname)) {
                            sensor->rack_iname = strdup(parent1);
                    } else {
                        if (0 != strcmp(parent1, sensor->rack_iname)) {
                            free(sensor->rack_iname);
                            sensor->rack_iname = strdup(parent1);
                        }
                    }
                    if (!(sensor->port)) {
                            sensor->port = strdup(port);
                    } else {
                        if (0 != strcmp(port, sensor->port)) {
                            free(sensor->port);
                            sensor->port = strdup(port);
                        }
                    }
                    sensor->temperature = TEMPERATURE;
                    sensor->humidity = HUMIDITY;
                } else {
                    // brand new sensor, just create it
                    sensor = create_sensor(name, TEMPERATURE, HUMIDITY, VALID);
                    sensor->rack_iname = strdup(parent1);
                    sensor->port = strdup(port);
                    zlist_append(self->sensors, sensor);
                    zlist_freefn(self->sensors, sensor, free_sensor, true);
                }
            }
        }
    }
    fty_proto_destroy (&asset);
    return 0;
}


//  --------------------------------------------------------------------------
//  Sensor env main actor
//
void
sensor_env_actor(zsock_t *pipe, void *args) {
    int i, rv;
    fty_sensor_env_server_t *self = fty_sensor_env_server_new();
    assert (self);
    zsock_signal (pipe, 0);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->mlm), NULL);
    if (!poller) {
        fty_sensor_env_server_destroy(&self);
        log_error ("zpoller_new () failed");
        return;
    }

    log_info ("Initializing device real paths.");
    for (i = 0; i < PORTMAP_LENGTH; ++i) {
        char *patha = realpath (portmapping[0][i], NULL);
        char *key = zsys_sprintf("%d", i + PORTS_OFFSET);
        if (patha) {
            zhash_insert(self->portmap, key, patha); // real link name
            zhash_freefn(self->portmap, key, freefn);
        } else {
            log_warning("Can't get realpath of %s, using %s: %s", portmapping[0][i], portmapping[1][i], strerror(errno));
            zhash_insert(self->portmap, key, (char *)portmapping[1][i]); // default
            zhash_freefn(self->portmap, key, freefn);
        }
        zstr_free(&key);
    }

    log_info ("Device real paths initiated.");
    uint64_t timestamp = (uint64_t) zclock_mono ();
    uint64_t timeout = (uint64_t) POLLING_INTERVAL;

    while (1) {
        log_trace ("cycle ... ");
        void *which = zpoller_wait (poller, timeout);
        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted) {
                log_info("server: zpoller terminated or zsys_interrupted");
                break;
            }
            if (zpoller_expired (poller)) {
                read_sensors (self);
            }
            timestamp = (uint64_t) zclock_mono ();
            continue;
        }
        else if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);
            if (cmd) {
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&msg);
                    break;
                }
                else if (streq (cmd, "BIND")) {
                    char *endpoint = zmsg_popstr (msg);
                    char *myname = zmsg_popstr (msg);
                    assert (endpoint && myname);
                    int rv = mlm_client_connect (self->mlm, endpoint, 1000, myname);
                    if (rv == -1) {
                        log_error (
                                "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                                endpoint, myname);
                        break;
                    }
                    log_info ("Connected to '%s' as '%s'", endpoint, myname);
                    zstr_free (&endpoint);
                    zstr_free (&myname);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (msg);
                    assert (stream);
                    rv = mlm_client_set_producer (self->mlm, stream);
                    if (rv == -1) {
                        mlm_client_destroy (&(self->mlm));
                        log_error (
                                "mlm_client_set_producer (stream = '%s') failed",
                                stream);
                        break;
                    }
                    log_info ("Publishing to '%s'", stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (msg);
                    char *pattern = zmsg_popstr (msg);
                    assert (stream && pattern);
                    rv = mlm_client_set_consumer (self->mlm, stream, pattern);
                    if (rv == -1) {
                        mlm_client_destroy (&(self->mlm));
                        log_error (
                                "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                                stream, pattern);
                        break;
                    }
                    log_info ("Subscribed to '%s'", stream);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq(cmd, "ASKFORASSETS")) {
                    log_debug("Asking for assets");
                    zmsg_t *republish = zmsg_new ();
                    rv = mlm_client_sendto (self->mlm, "asset-agent", "REPUBLISH", NULL, 5000, &republish);
                    if ( rv != 0) {
                        log_error ("Cannot send REPUBLISH message");
                    }
                }
                else {
                    log_debug ("Unknown command.");
                }

                zstr_free (&cmd);
            }
            zmsg_destroy (&msg);
        }
        else {
            uint64_t now = (uint64_t) zclock_mono ();
            if (now - timestamp >= timeout) {
                read_sensors (self);
                timestamp = (uint64_t) zclock_mono ();
            }
            zmsg_t *msg = mlm_client_recv (self->mlm);
            if (!msg)
                break;

            handle_proto_sensor(self, msg);
            // zmsg_destroy (&msg); // called within handle_proto_sensor->fty_proto_decode
        }
    }
    log_info("server: about to quit");

    zpoller_destroy (&poller);
    fty_sensor_env_server_destroy(&self);
    log_info("server: finished");
    return;
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_sensor_env_server_test (bool verbose)
{

    printf (" * fty_sensor_env_server: ");
    testing = 1;

    //  @selftest
    //  Simple create/destroy test

    // Note: If your selftest reads SCMed fixture data, please keep it in
    // src/selftest-ro; if your test creates filesystem objects, please
    // do so under src/selftest-rw. They are defined below along with a
    // usecase (asert) to make compilers happy.
    const char *SELFTEST_DIR_RO = "src/selftest-ro";
    const char *SELFTEST_DIR_RW = "src/selftest-rw";
    assert (SELFTEST_DIR_RO);
    assert (SELFTEST_DIR_RW);
    // Uncomment these to use C++ strings in C++ selftest code:
    //std::string str_SELFTEST_DIR_RO = std::string(SELFTEST_DIR_RO);
    //std::string str_SELFTEST_DIR_RW = std::string(SELFTEST_DIR_RW);
    //assert ( (str_SELFTEST_DIR_RO != "") );
    //assert ( (str_SELFTEST_DIR_RW != "") );
    // NOTE that for "char*" context you need (str_SELFTEST_DIR_RO + "/myfilename").c_str()

    // Test #1: unit tests for functions first
    fty_sensor_env_server_t *self = fty_sensor_env_server_new ();
    assert(self);
    assert(self->mlm);
    assert(self->portmap);
    assert(self->sensors);
    mlm_client_connect (self->mlm, "ipc://@/malamute", 1000, "fty-sensor-env");
    mlm_client_set_producer (self->mlm, FTY_PROTO_STREAM_METRICS_SENSOR);
    mlm_client_set_consumer (self->mlm, FTY_PROTO_STREAM_ASSETS, ".*");

    // test sensors - as we support adding sensors for all reasons possible, every check should be positive
    // ===== sensors ==============================================================================
    external_sensor_t *sensor = create_sensor("test sensor", TEMPERATURE, HUMIDITY, VALID); // verify temperature and humidity sensor can be added as valid
    assert(sensor);
    free_sensor(sensor); // verify sensor is properly freed
    free_sensor(NULL); // verify free function won't fail with NULL pointer
    // add sensors to list
    sensor = create_sensor("test sensor 1", TEMPERATURE, HUMIDITY, VALID); // verify temperature and humidity sensor can be added as valid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("1");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 2", DISABLED, HUMIDITY, VALID); // verify humidity sensor can be added as valid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("2");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 3", TEMPERATURE, DISABLED, VALID); // verify temperature sensor can be added as valid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("3");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 4", DISABLED, DISABLED, VALID); // verify sensor can be added as valid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("4");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 5", TEMPERATURE, HUMIDITY, INVALID); // verify temperature and humidity sensor can be added as invalid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("5");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 6", DISABLED, HUMIDITY, INVALID); // verify humidity sensor can be added as invalid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("6");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 7", TEMPERATURE, DISABLED, INVALID); // verify temperature sensor can be added as invalid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("7");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    sensor = create_sensor("test sensor 8", DISABLED, DISABLED, INVALID); // verify sensor can be added as invalid
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("8");
    assert(sensor);
    zlist_append(self->sensors, sensor);
    zlist_freefn(self->sensors, sensor, free_sensor, true);
    // search sensor
    sensor = NULL;
    sensor = search_sensor(self->sensors, "test sensor 3");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "test sensor 3")); // verify search works
    assert(TEMPERATURE == sensor->temperature);
    assert(DISABLED == sensor->humidity);
    assert(VALID == sensor->valid);
    sensor = NULL;
    sensor = search_sensor(self->sensors, "test sensor 3"); // verify search won't delete searched item
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "test sensor 3"));
    assert(TEMPERATURE == sensor->temperature);
    assert(DISABLED == sensor->humidity);
    assert(VALID == sensor->valid);
    // remove sensors from list
    zlist_purge(self->sensors);
    // ===== /sensors =============================================================================

    // ===== get_measurement function =============================================================
    testing = 2; // sets file open to pass
    fty_proto_t* msg = get_measurement(TEMPERATURE, "dummy"); // verify temperature works fine
    assert(msg);
    assert(FTY_PROTO_METRIC == fty_proto_id(msg));
    assert(streq(fty_proto_value(msg),"0.01"));
    assert(streq(fty_proto_unit(msg),"C"));
    fty_proto_destroy(&msg);
    msg = get_measurement(HUMIDITY, "dummy"); // verify humidity works fine
    assert(msg);
    assert(FTY_PROTO_METRIC == fty_proto_id(msg));
    assert(streq(fty_proto_value(msg),"0.01"));
    assert(streq(fty_proto_unit(msg),"%"));
    fty_proto_destroy(&msg);
    msg = get_measurement(1, "dummy"); // verify gpi works fine
    assert(msg);
    assert(FTY_PROTO_METRIC == fty_proto_id(msg));
    assert(streq(fty_proto_value(msg),"closed"));
    assert(streq(fty_proto_unit(msg),""));
    fty_proto_destroy(&msg);
    msg = get_measurement(DISABLED, "dummy"); // verify disabled check returns NULL
    assert(NULL == msg);
    testing = 1; // sets file open to fail
    msg = get_measurement(HUMIDITY, "fail"); // verify measurement returns NULL when file open fails
    assert(NULL == msg);
    testing = 2; // sets file open to pass
    // ===== /get_measurement function ============================================================

    // ===== send_message function ================================================================
    msg = get_measurement(TEMPERATURE, "dummy");
    sensor = create_sensor("test sensor 1", TEMPERATURE, HUMIDITY, VALID);
    sensor->rack_iname = strdup("dummyrackcontroller-1");
    sensor->port = strdup("1");
    int rv = send_message(NULL, msg, sensor, HUMIDITY_STR "./dummy", "dummysensor-1", NULL); // verify function fails with wrong arguments
    assert(1 == rv);
    rv = send_message(self->mlm, NULL, sensor, HUMIDITY_STR "./dummy", "dummysensor-1", NULL); // verify function fails with wrong arguments
    assert(1 == rv);
    rv = send_message(self->mlm, msg, NULL, HUMIDITY_STR "./dummy", "dummysensor-1", NULL); // verify function fails with wrong arguments
    assert(1 == rv);
    rv = send_message(self->mlm, msg, sensor, NULL, "dummysensor-1", NULL); // verify function fails with wrong arguments
    assert(1 == rv);
    rv = send_message(self->mlm, msg, sensor, HUMIDITY_STR "./dummy", NULL, NULL); // verify function fails with wrong arguments
    assert(1 == rv);
    rv = send_message(self->mlm, msg, sensor, HUMIDITY_STR "./dummy", "dummysensor-1", NULL); // verify function succeeds for regular sensors
    assert(0 == rv);
    msg = get_measurement(TEMPERATURE, "dummy");
    rv = send_message(self->mlm, msg, sensor, STATUSGPI_STR "1./dummy", "dummygpiosensor-1", "1"); // verify function succeeds for regular sensors
    assert(0 == rv);
    free_sensor(sensor);
    // ===== /send_message function ===============================================================

    // ===== handle_proto_sensor function =========================================================
    // add regular sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    zhash_t *aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensor");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    zhash_t *ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "1");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensor-1");
    zmsg_t *message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensor is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(1 == zlist_size(self->sensors));
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(0 == strcmp(sensor->rack_iname, "rackcontroller-0"));
    assert(TEMPERATURE == sensor->temperature);
    assert(HUMIDITY == sensor->humidity);
    assert(VALID == sensor->valid);
    assert(streq("1", sensor->port));
    // update regular sensor to different parent
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_UPDATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensor");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "1");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensor-1");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensor is updated properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(1 == zlist_size(self->sensors));
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(0 == strcmp(sensor->rack_iname, "rackcontroller-0"));
    assert(TEMPERATURE == sensor->temperature);
    assert(HUMIDITY == sensor->humidity);
    assert(VALID == sensor->valid);
    assert(streq("1", sensor->port));
    // add another sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensor");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "2");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensor-2");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensor is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-2");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-2"));
    assert(TEMPERATURE == sensor->temperature);
    assert(HUMIDITY == sensor->humidity);
    assert(VALID == sensor->valid);
    // delete sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_DELETE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensor");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "2");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensor-2");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensor is removed properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-2");
    assert(NULL == sensor);
    // add GPI sensor to existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-1");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "1");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-1");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    char *sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "1")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-1", zhash_cursor(sensor->gpi)));
    // add another GPI sensor to existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-1");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "2");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-2");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "2")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-2", zhash_cursor(sensor->gpi)));
    // delete sensor GPI
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_DELETE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-1");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "2");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-2");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is removed properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "2")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(NULL == sensor_gpi_port);
    // add GPI sensor to non-existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-3");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "3");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-3");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-3");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-3"));
    assert(INVALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "3")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-3", zhash_cursor(sensor->gpi)));
    // add regular sensor to make non-existing sensor valid
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensor");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "3");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensor-3");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensor is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-3");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-3"));
    assert(TEMPERATURE == sensor->temperature);
    assert(HUMIDITY == sensor->humidity);
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "3")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-3", zhash_cursor(sensor->gpi)));
    // add GPI sensor to non-existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-4");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "4");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-4");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-4");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-4"));
    assert(INVALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "4")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-4", zhash_cursor(sensor->gpi)));
    // delete GPI sensor with non-existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_DELETE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-4");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "4");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-4");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify whole sensor is removed
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-4");
    assert(NULL == sensor);
    // update regular sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_UPDATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensor");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "51");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensor-1");
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(TEMPERATURE == sensor->temperature);
    assert(HUMIDITY == sensor->humidity);
    assert(VALID == sensor->valid);
    assert(streq("1", sensor->port));
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensor is updated properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(TEMPERATURE == sensor->temperature);
    assert(HUMIDITY == sensor->humidity);
    assert(VALID == sensor->valid);
    assert(streq("51", sensor->port));
    // update GPI sensor to existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_UPDATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-1");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "101");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-1");
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    assert(streq("51",sensor->port));
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "1")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-1", zhash_cursor(sensor->gpi)));
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    assert(streq("51",sensor->port));
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "1")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(NULL == sensor_gpi_port);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "101")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-1", zhash_cursor(sensor->gpi)));
    // add another GPI sensor to existing sensor
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-1");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "5");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-5");
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is added properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "5")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-5", zhash_cursor(sensor->gpi)));
    // update GPI sensor (move it to a different Env sensor)
    msg = fty_proto_new (FTY_PROTO_ASSET);
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_UPDATE);
    aux = zhash_new();
    zhash_autofree(aux);
    zhash_insert(aux, "type", "device");
    zhash_insert(aux, "subtype", "sensorgpio");
    zhash_insert(aux, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, "dummysensor-3");
    zhash_insert(aux, "parent_name.2", "rackcontroller-0");
    fty_proto_set_aux(msg, &aux);
    ext = zhash_new();
    zhash_autofree(ext);
    zhash_insert(ext, FTY_PROTO_ASSET_EXT_PORT, "5");
    fty_proto_set_ext(msg, &ext);
    fty_proto_set_name(msg, "dummysensorgpi-5");
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "5")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-5", zhash_cursor(sensor->gpi)));
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify sensorgpi is changed properly
    assert(0 == rv);
    sensor = search_sensor(self->sensors, "dummysensor-1");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-1"));
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "5")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(NULL == sensor_gpi_port);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    sensor = search_sensor(self->sensors, "dummysensor-3");
    assert(sensor);
    assert(0 == strcmp(sensor->iname, "dummysensor-3"));
    assert(VALID == sensor->valid);
    sensor_gpi_port = (char *) zhash_first(sensor->gpi);
    while (sensor_gpi_port) {
        if (streq(sensor_gpi_port, "5")) break;
        sensor_gpi_port = (char *) zhash_next(sensor->gpi);
    }
    assert(sensor_gpi_port);
    assert(streq("dummysensorgpi-5", zhash_cursor(sensor->gpi)));
    // try to handle invalid message
    msg = fty_proto_new (FTY_PROTO_METRIC);
    message = fty_proto_encode (&msg);
    rv = handle_proto_sensor(self, message); // verify wrong message is rejected
    assert(-1 == rv);
    // ===== /handle_proto_sensor function ========================================================

    // ===== read_sensors function ================================================================
    read_sensors (self); // just verify there will be no crash
    // ===== /read_sensors function ===============================================================
    // close tests
    fty_sensor_env_server_destroy (&self);
    //  @end

    testing = 0;
    printf ("OK\n");
}
