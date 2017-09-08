/*  =========================================================================
    fty_sensor_env_server - Grab temperature and humidity data from sensors attached to the box

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

// ### logging
int agent_th_verbose = 0;
#define zsys_debug(...) \
    do { if (agent_th_verbose) zsys_debug (__VA_ARGS__); } while (0);

#define PORTMAP_LENGTH 4
const char *portmapping[2][PORTMAP_LENGTH] = {
        { "/dev/ttySTH1", "/dev/ttySTH2", "/dev/ttySTH3", "/dev/ttySTH4" },
        { "/dev/ttyS9",   "/dev/ttyS10",  "/dev/ttyS11",  "/dev/ttyS12"  } };

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
    zlist_t         *sensors;
};


//  --------------------------------------------------------------------------
//  Properly free a sensor

void
free_sensor(external_sensor_t **sensor) {
    if ((*sensor)->iname) free((*sensor)->iname);
    if ((*sensor)->rack_iname) free((*sensor)->rack_iname);
    if ((*sensor)->port) free((*sensor)->port);
    zhash_destroy(&((*sensor)->gpi));
    free(*sensor);
    sensor = NULL;
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
    }
        zsys_error ("mlm_client_new () failed");
        return NULL;
    self->portmap = zhash_new();
    if (!(self->portmap)) {
        zsys_error ("mlm_client_new () failed");
        return NULL;
    }
    zhash_autofree(self->portmap);
    self->sensors = zlist_new ();
    if (!(self->sensors)) {
        zsys_error ("sensors zlist_new () failed");
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
        external_sensor_t *sensor = (external_sensor_t *) zlist_first(self->sensors);
        while (sensor) {
            free_sensor(&sensor);
            sensor = (external_sensor_t *) zlist_next(self->sensors);
        }
        zlist_destroy (&(self->sensors));
        zhash_destroy (&(self->portmap));
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


//  --------------------------------------------------------------------------
//  Send message containing sensor values

static void
send_message(mlm_client_t *client, fty_proto_t *msg, const external_sensor_t *sensor,
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


//  --------------------------------------------------------------------------
//  Attempt to read values from sensors and publish results

static void
read_sensors (mlm_client_t *client, fty_sensor_env_server_t *self)
{
    assert (client);
    external_sensor_t *sensor = (external_sensor_t *) zlist_first(self->sensors);
    while (NULL != sensor) {
        const char *port_file = (char *) zhash_lookup(self->portmap, sensor->port);
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
        sensor = (external_sensor_t *) zlist_next(self->sensors);
    }
}


//  --------------------------------------------------------------------------
//  Read ASSET data containing sensor or sensorgpio and add them to sensors list

void
handle_proto_sensor(fty_sensor_env_server_t *self, zmsg_t *message) {
    fty_proto_t *asset = fty_proto_decode (&message);
    if (!asset || fty_proto_id (asset) != FTY_PROTO_ASSET) {
        fty_proto_destroy (&asset);
        zsys_warning ("fty_proto_decode () failed OR received message not FTY_PROTO_ASSET");
        return;
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
            fty_proto_destroy (&asset);
            return;
        }
        if (0 == strncmp(subtype, "sensorgpio", strlen("sensorgpio"))) {
            external_sensor_t *sensor = (external_sensor_t *)search_sensor(self->sensors, parent1);
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
                    zlist_append(self->sensors, sensor);
                }
            } else if (streq (operation, FTY_PROTO_ASSET_OP_DELETE) || streq (operation, FTY_PROTO_ASSET_OP_RETIRE)) {
                // simple delete
                if (sensor) {
                    zhash_delete(sensor->gpi, name);
                }
            }
        }
        else if (0 == strncmp(subtype, "sensor", strlen("sensor"))) {
            external_sensor_t *sensor = (external_sensor_t *)search_sensor(self->sensors, parent1);
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
                    zlist_append(self->sensors, sensor);
                }
            } else if (streq (operation, FTY_PROTO_ASSET_OP_DELETE) || streq (operation, FTY_PROTO_ASSET_OP_RETIRE)) {
                // simple delete with deallocation
                if (sensor) {
                    zlist_remove(self->sensors, sensor);
                    free_sensor(&sensor);
                }
            }
        }
    } 
    fty_proto_destroy (&asset);
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

    zpoller_t *poller = zpoller_new (mlm_client_msgpipe (self->mlm), NULL);
    if (!poller) {
        fty_sensor_env_server_destroy(&self);
        zsys_error ("zpoller_new () failed");
        return;
    }

    zsys_info ("Initializing device real paths.");
    for (i = 0; i < PORTMAP_LENGTH; ++i) {
        char *patha = realpath (portmapping[0][i], NULL);
        char *key = zsys_sprintf("%d", i + PORTS_OFFSET);
        if (patha) {
            zhash_insert(self->portmap, key, patha); // real link name
            zhash_freefn(self->portmap, key, freefn);
        } else {
            zsys_warning("Can't get realpath of %s, using %s: %s", portmapping[0][i], portmapping[1][i], strerror(errno));
            zhash_insert(self->portmap, key, (char *)portmapping[1][i]); // default
            zhash_freefn(self->portmap, key, freefn);
        }
        zstr_free(&key);
    }

    zsys_info ("Device real paths initiated.");
    uint64_t timestamp = (uint64_t) zclock_mono ();
    uint64_t timeout = (uint64_t) POLLING_INTERVAL;

    while (!zsys_interrupted) {
        zsys_debug ("cycle ... ");
        void *which = zpoller_wait (poller, timeout);
        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted)
                break;
            if (zpoller_expired (poller)) {
                read_sensors (self->mlm, self);
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
                        zsys_error (
                                "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                                endpoint, myname);
                        break;
                    }
                    zsys_info ("Connected to '%s' as '%s'", endpoint, myname);
                    zstr_free (&endpoint);
                    zstr_free (&myname);
                }
                else if (streq (cmd, "PRODUCER")) {
                    char *stream = zmsg_popstr (msg);
                    assert (stream);
                    rv = mlm_client_set_producer (self->mlm, stream);
                    if (rv == -1) {
                        mlm_client_destroy (&(self->mlm));
                        zsys_error (
                                "mlm_client_set_producer (stream = '%s') failed",
                                stream);
                        break;
                    }
                    zsys_info ("Publishing to '%s'", stream);
                    zstr_free (&stream);
                }
                else if (streq (cmd, "CONSUMER")) {
                    char *stream = zmsg_popstr (msg);
                    char *pattern = zmsg_popstr (msg);
                    assert (stream && pattern);
                    rv = mlm_client_set_consumer (self->mlm, stream, pattern);
                    if (rv == -1) {
                        mlm_client_destroy (&(self->mlm));
                        zsys_error (
                                "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                                stream, pattern);
                        break;
                    }
                    zsys_info ("Subscribed to '%s'", stream);
                    zstr_free (&stream);
                    zstr_free (&pattern);
                }
                else if (streq(cmd, "ASKFORASSETS")) {
                    zmsg_t *republish = zmsg_new ();
                    rv = mlm_client_sendto (self->mlm, "asset-agent", "REPUBLISH", NULL, 5000, &republish);
                    if ( rv != 0) {
                        zsys_error ("Cannot send REPUBLISH message");
                    }
                    zmsg_destroy (&republish);
                }
                else if (streq (cmd, "VERBOSE")) {
                    agent_th_verbose = 1;
                    zsys_debug ("mlm_client_set_verbose");
                    mlm_client_set_verbose (self->mlm, 1);
                }
                else {
                    zsys_debug ("Unknown command.");
                }

                zstr_free (&cmd);
            }
            zmsg_destroy (&msg);
        }
        else {
            uint64_t now = (uint64_t) zclock_mono ();
            if (now - timestamp >= timeout) {
                read_sensors (self->mlm, self);
                timestamp = (uint64_t) zclock_mono ();
            }
            zmsg_t *msg = mlm_client_recv (self->mlm);
            if (!msg)
                break;

            handle_proto_sensor(self, msg);

            zmsg_destroy (&msg);
        }
    }

    zpoller_destroy (&poller);
    fty_sensor_env_server_destroy(&self);
    return;
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
fty_sensor_env_server_test (bool verbose)
{
    
    printf (" * fty_sensor_env_server: ");

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

    fty_sensor_env_server_t *self = fty_sensor_env_server_new ();
    assert (self);
    fty_sensor_env_server_destroy (&self);
    //  @end
    printf ("OK\n");
}
