/*
Copyright (C) 2014 - 2017 Eaton

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
*/

/*!
 * \file fty_sensor_env.cc
 * \author Michal Hrusecky <MichalHrusecky@Eaton.com>
 * \author Tomas Halman <TomasHalman@Eaton.com>
 * \author Jim Klimov <EvgenyKlimov@Eaton.com>
 * \author Jiri Kukacka <JiriKukacka@Eaton.com>
 * \brief Not yet documented file
 */

#include "../include/fty_sensor_env_library.h"
#include "../include/fty_sensor_env.h"

#define PORTS_OFFSET                9   // ports range 9-12
#define POLLING_INTERVAL            5000
#define TIME_TO_LIVE                300
#define AGENT_NAME                  "fty-sensor-env"

// ### logging
int agent_th_verbose = 0;
#define zsys_debug(...) \
    do { if (agent_th_verbose) zsys_debug (__VA_ARGS__); } while (0);

#define PORTMAP_LENGTH 4
const char *portmapping[2][PORTMAP_LENGTH] = {
        { "/dev/ttySTH1", "/dev/ttySTH2", "/dev/ttySTH3", "/dev/ttySTH4" },
        { "/dev/ttyS9",   "/dev/ttyS10",  "/dev/ttyS11",  "/dev/ttyS12"  } };

struct c_item {
    int32_t T;
    int32_t H;
};

typedef struct _ext_sensor {
    char    *iname;
    char    *rack_iname;
    char    *port;
    char    temperature;
    char    humidity;
    zhash_t *gpi;
    char    valid;
} external_sensor;

static void freefn(void *sensorgpi)
{
    if (sensorgpi) free(sensorgpi);
}

external_sensor *search_sensor(zlist_t *sensor_list, const char *iname) {
    external_sensor *sensor = (external_sensor *) zlist_first(sensor_list);
    while (sensor) {
        if (0 == strcmp(sensor->iname, iname)) {
            break;
        }
        sensor = (external_sensor *) zlist_next(sensor_list);
    }
    return sensor;
}

external_sensor *create_sensor(const char *name, const char temperature, const char humidity, const char valid) {
    external_sensor *sensor = (external_sensor *) malloc(sizeof(external_sensor));
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

void free_sensor(external_sensor **sensor) {
    if ((*sensor)->iname) free((*sensor)->iname);
    if ((*sensor)->rack_iname) free((*sensor)->rack_iname);
    if ((*sensor)->port) free((*sensor)->port);
    zhash_destroy(&((*sensor)->gpi));
    free(*sensor);
    sensor = NULL;
}

fty_proto_t*
get_measurement (const char what, const char *port_file) {
    if (DISABLED == what) {
        return NULL;
    }
    fty_proto_t* ret = fty_proto_new (FTY_PROTO_METRIC);
    c_item data = { 0, 0 };

    int fd = open_device(port_file);
    if(!device_connected(fd)) {
        if(fd > 0)
            close(fd);
        zsys_debug("No sensor attached to %s", port_file);
        fty_proto_destroy (&ret);
        return NULL;
    }
    else {
        reset_device(fd);
        if (TEMPERATURE == what) {
            data.T = get_th_data(fd, MEASURE_TEMP);
            compensate_temp(data.T, &data.T);
            zsys_debug("Got data from sensor '%s' - T = %" PRId32 ".%02" PRId32 " C", port_file, data.T/100, data.T%100);

            fty_proto_set_value (ret, "%.2f", data.T / (float) 100);
            fty_proto_set_unit (ret, "%s", "C");

            zsys_debug ("Returning T = %s C", fty_proto_value (ret));
        } else if (HUMIDITY == what) {
            data.T = get_th_data(fd, MEASURE_TEMP);
            data.H = get_th_data(fd, MEASURE_HUMI);
            compensate_humidity(data.H, data.T, &data.H);
            zsys_debug("Got data from sensor '%s' - H = %" PRId32 ".%02" PRId32 " %%", port_file, data.H/100, data.H%100);

            fty_proto_set_value (ret, "%.2f", data.H / (float) 100);
            fty_proto_set_unit (ret, "%s", "%");

            zsys_debug ("Returning H = %s %%", fty_proto_value (ret));
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

            zsys_debug ("Returning S = %s", fty_proto_value (ret));
        }
        close(fd);
    }
    return ret;
}

static void send_message(mlm_client_t *client, fty_proto_t *msg, const external_sensor *sensor,
        const char *type, const char *sname, const char *ext_port) {
    if (msg == NULL) {
        return;
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
    if (rv != 0) {
        zsys_error ("mlm_client_send (subject = '%s') failed", subject);
    }
    zstr_free(&subject);
    fty_proto_destroy (&msg);
}

static void
read_sensors (mlm_client_t *client, zlist_t *my_sensors, zhash_t *portmap)
{
    assert (client);
    external_sensor *sensor = (external_sensor *) zlist_first(my_sensors);
    while (NULL != sensor) {
        const char *port_file = (char *) zhash_lookup(portmap, sensor->port);
        zsys_debug ("Measuring '%s%s'", TH, sensor->port);
        zsys_debug ("Reading from '%s'", port_file);
        fty_proto_t* msg = get_measurement(TEMPERATURE, port_file);
        if (msg) {
            char *type = zsys_sprintf("%s.%s", TEMPERATURE_STR, port_file);
            send_message(client, msg, sensor, type, sensor->iname, NULL);
            zstr_free(&type);
        }
        msg = get_measurement(HUMIDITY, port_file);
        if (msg) {
            char *type = zsys_sprintf("%s.%s", HUMIDITY_STR, port_file);
            send_message(client, msg, sensor, type, sensor->iname, NULL);
            zstr_free(&type);
        }
        char *sensor_gpi_port = (char *) zhash_first(sensor->gpi);
        while (sensor_gpi_port) {
            int sensor_gpi_port_num = atoi(sensor_gpi_port);
            msg = get_measurement(sensor_gpi_port_num, port_file);
            if (msg) {
                char *type = zsys_sprintf("%s%s.%s", STATUSGPI_STR, sensor_gpi_port, port_file);
                send_message(client, msg, sensor, type, (char *) zhash_cursor(sensor->gpi), sensor_gpi_port);
                zstr_free(&type);
            }
            sensor_gpi_port = (char *) zhash_next(sensor->gpi);
        }
        sensor = (external_sensor *) zlist_next(my_sensors);
    }
}

int
main (int argc, char *argv []) {

    const char *endpoint = "ipc://@/malamute";
    const char *addr = (argc == 1) ? "ipc://@/malamute" : argv[1];
    int i;
    char *fty_log_level = getenv ("BIOS_LOG_LEVEL");
    if (fty_log_level && streq (fty_log_level, "LOG_DEBUG"))
        agent_th_verbose = 1;

    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        zsys_error ("mlm_client_new () failed");
        return EXIT_FAILURE;
    }

    if (fty_log_level && streq (fty_log_level, "LOG_DEBUG")) {
        zsys_debug ("mlm_client_set_verbose");
        mlm_client_set_verbose (client, 1);
    }

    int rv = mlm_client_connect (client, addr, 1000, AGENT_NAME);
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                addr, AGENT_NAME);
        return EXIT_FAILURE;
    }
    zsys_info ("Connected to '%s'", endpoint);

    rv = mlm_client_set_consumer (client, FTY_PROTO_STREAM_ASSETS, ".*");
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                FTY_PROTO_STREAM_ASSETS, ".*");
        return EXIT_FAILURE;
    }
    zsys_info ("Subscribed to '%s'", FTY_PROTO_STREAM_ASSETS);

    rv = mlm_client_set_producer (client, FTY_PROTO_STREAM_METRICS_SENSOR);
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_set_producer (stream = '%s') failed",
                FTY_PROTO_STREAM_METRICS_SENSOR);
        return EXIT_FAILURE;
    }
    zsys_info ("Publishing to '%s' as '%s'", FTY_PROTO_STREAM_METRICS_SENSOR, AGENT_NAME);

    zpoller_t *poller = zpoller_new (mlm_client_msgpipe (client), NULL);
    if (!poller) {
        mlm_client_destroy (&client);
        zsys_error ("zpoller_new () failed");
        return EXIT_FAILURE;
    }

    zsys_info ("Initializing device real paths.");
    zhash_t *portmap = zhash_new();
    zhash_autofree(portmap);
    for (i = 0; i < PORTMAP_LENGTH; ++i) {
        char *patha = realpath (portmapping[0][i], NULL);
        char *key = zsys_sprintf("%d", i + PORTS_OFFSET);
        if (patha) {
            zhash_insert(portmap, key, patha); // real link name
            zhash_freefn(portmap, key, freefn);
        } else {
            zsys_warning("Can't get realpath of %s, using %s: %s", portmapping[0][i], portmapping[1][i], strerror(errno));
            zhash_insert(portmap, key, (char *)portmapping[1][i]); // default
            zhash_freefn(portmap, key, freefn);
        }
        zstr_free(&key);
    }  
    zsys_info ("Device real paths initiated.");

    uint64_t timestamp = (uint64_t) zclock_mono ();
    uint64_t timeout = (uint64_t) POLLING_INTERVAL;

    zlist_t *my_sensors = zlist_new ();

    // instead of keeping state file with all sensors, act like other agents and ask for republish
    zmsg_t *republish = zmsg_new ();
    rv = mlm_client_sendto (client, "asset-agent", "REPUBLISH", NULL, 5000, &republish);
    if ( rv != 0) {
        zsys_error ("Cannot send REPUBLISH message");
    }
    zmsg_destroy (&republish);

    while (!zsys_interrupted) {
        zsys_debug ("cycle ... ");
        void *which = zpoller_wait (poller, timeout);

        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted)
                break;
            if (zpoller_expired (poller)) {
                read_sensors (client, my_sensors, portmap);
            }
            timestamp = (uint64_t) zclock_mono ();
            continue;
        }

        uint64_t now = (uint64_t) zclock_mono ();
        if (now - timestamp >= timeout) {
            read_sensors (client, my_sensors, portmap);
            timestamp = (uint64_t) zclock_mono ();
        }

        zmsg_t *message = mlm_client_recv (client);
        if (!message)
            break;

        fty_proto_t *asset = fty_proto_decode (&message);
        if (!asset || fty_proto_id (asset) != FTY_PROTO_ASSET) {
            fty_proto_destroy (&asset);
            zsys_warning ("fty_proto_decode () failed OR received message not FTY_PROTO_ASSET");
            continue;
        }
        const char *operation = fty_proto_operation (asset);
        const char *type = fty_proto_aux_string (asset, "type", "");
        const char *subtype = fty_proto_aux_string (asset, "subtype", "");
        const char *name = fty_proto_name (asset);
        zsys_info ("Received an asset message, operation = '%s', name = '%s', type = '%s', subtype = '%s'",
                operation, name, type, subtype);

        if (0 == strcmp(type, "device")) {
            const char *port = fty_proto_ext_string(asset, FTY_PROTO_ASSET_EXT_PORT, NULL);
            const char *parent1 = fty_proto_aux_string(asset, FTY_PROTO_ASSET_AUX_PARENT_NAME_1, NULL);
            if (!port || !parent1) {
                continue;
            }
            if (0 == strncmp(subtype, "sensorgpio", strlen("sensorgpio"))) {
                external_sensor *sensor = (external_sensor *)search_sensor(my_sensors, parent1);
                if (streq (operation, FTY_PROTO_ASSET_OP_CREATE) || streq (operation, FTY_PROTO_ASSET_OP_UPDATE)) {
                    if (sensor) {
                        // add GPI sensor to Sensor
                        zhash_update(sensor->gpi, name, (char *)port);
                        zhash_freefn(sensor->gpi, name, freefn);
                    } else {
                        // create Sensor as it seems we don't know it yet, and mark if for update
                        sensor = create_sensor(parent1, DISABLED, DISABLED, INVALID);
                        zhash_update(sensor->gpi, name, (char *)port);
                        zhash_freefn(sensor->gpi, name, freefn);
                        zlist_append(my_sensors, sensor);
                    }
                } else if (streq (operation, FTY_PROTO_ASSET_OP_DELETE) || streq (operation, FTY_PROTO_ASSET_OP_RETIRE)) {
                    // simple delete
                    if (sensor) {
                        zhash_delete(sensor->gpi, name);
                    }
                }
            }
            else if (0 == strncmp(subtype, "sensor", strlen("sensor"))) {
                external_sensor *sensor = (external_sensor *)search_sensor(my_sensors, parent1);
                if (streq (operation, FTY_PROTO_ASSET_OP_CREATE) || streq (operation, FTY_PROTO_ASSET_OP_UPDATE)) {
                    if (sensor) {
                        // update sensor
                        sensor->valid = VALID;
                        if (0 != strcmp(parent1, sensor->rack_iname)) {
                            free(sensor->rack_iname);
                            sensor->rack_iname = strdup(parent1);
                        }
                        if (0 != strcmp(port, sensor->port)) {
                            free(sensor->port);
                            sensor->port = strdup(port);
                        }
                        sensor->temperature = TEMPERATURE;
                        sensor->humidity = HUMIDITY;
                    } else {
                        // brand new sensor, just create it
                        sensor = create_sensor(name, TEMPERATURE, HUMIDITY, VALID);
                        sensor->rack_iname = strdup(parent1);
                        sensor->port = strdup(port);
                        zlist_append(my_sensors, sensor);
                    }
                } else if (streq (operation, FTY_PROTO_ASSET_OP_DELETE) || streq (operation, FTY_PROTO_ASSET_OP_RETIRE)) {
                    // simple delete with deallocation
                    if (sensor) {
                        zlist_remove(my_sensors, sensor);
                        free_sensor(&sensor);
                    }
                }
            }
        } 
        fty_proto_destroy (&asset);
    }

    external_sensor *sensor = (external_sensor *) zlist_first(my_sensors);
    while (sensor) {
        free_sensor(&sensor);
        sensor = (external_sensor *) zlist_next(my_sensors);
    }
    zhash_destroy (&portmap);
    zlist_destroy (&my_sensors);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    return 0;
}
