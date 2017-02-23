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
 * \file agent-th.cc
 * \author Michal Hrusecky <MichalHrusecky@Eaton.com>
 * \author Tomas Halman <TomasHalman@Eaton.com>
 * \author Jim Klimov <EvgenyKlimov@Eaton.com>
 * \brief Not yet documented file
 */

#include "../include/fty_sensor_env_library.h"

#include <vector>
#include <map>
#include <string>
#include <cmath>

#define POLLING_INTERVAL            5000
#define TIME_TO_LIVE                300

// ### logging
bool agent_th_verbose = false;
#define zsys_debug(...) \
    do { if (agent_th_verbose) zsys_debug (__VA_ARGS__); } while (0);

// temporary
#define HOSTNAME_FILE "/var/lib/fty/composite-metrics/agent_th"

fty_proto_t*
get_measurement (char* what);

// strdup is to avoid -Werror=write-strings - not enough time to rewrite it properly
// + it's going to be proprietary code ;-)
char *vars[] = {
    strdup ("temperature.TH1"),
    strdup ("humidity.TH1"),
    strdup ("temperature.TH2"),
    strdup ("humidity.TH2"),
    strdup ("temperature.TH3"),
    strdup ("humidity.TH3"),
    strdup ("temperature.TH4"),
    strdup ("humidity.TH4"),
    NULL
};

// maps symbolic name to real device name!!
std::map <std::string, std::string> devmap = {
    {"/dev/ttySTH1", "/dev/ttyS9"},
    {"/dev/ttySTH2", "/dev/ttyS10"},
    {"/dev/ttySTH3", "/dev/ttyS11"},
    {"/dev/ttySTH4", "/dev/ttyS12"}
};

struct sample_agent {
    const char* agent_name;   //!< Name of the measuring agent
    char **variants;          //!< Various sources to iterate over
    const char* measurement;  /*!< Printf formated string for what are we
                                   measuring, %s will be filled with source */
    const char* at;           /*!< Printf formated string for where are we
                                   measuring, %s will be filled with hostname */
    int32_t diff;             /*!< Minimum difference required for publishing */
    fty_proto_t* (*get_measurement)(char* what); //!< Measuring itself
};

sample_agent agent = {
    "agent-th",
    vars,
    "%s",
    "%s",
    50,
    get_measurement
};

struct c_item {
    time_t time;
    bool broken;
    int32_t T;
    int32_t H;
};

fty_proto_t*
get_measurement (char* what) {

    fty_proto_t* ret = NULL;
    char *th = strdup(what + (strlen(what) - 3));
    c_item data = { 0, false, 0, 0 }, *data_p = NULL;
    zsys_debug ("Measuring '%s'", what);

    // translate /dev/ttySTH1 - TH4 to /dev/ttyS9 using devmap hash map
    // defaults will simply work everywhere, for revision 00 code in main
    // should deail with it nicelly
    std::string tpath {"/dev/ttyS"};
    tpath.append (th);
    std::string path = devmap.at (tpath);

    zsys_debug ("Reading from '%s'", path.c_str());
    data.time = time(NULL);

    int fd = open_device(path.c_str());
    if(!device_connected(fd)) {
        if(fd > 0)
            close(fd);
        zsys_debug("No sensor attached to %s", path.c_str());
        data.broken = true;
    }
    else {
        reset_device(fd);
        data.T = get_th_data(fd, MEASURE_TEMP);
        data.H = get_th_data(fd, MEASURE_HUMI);
        compensate_temp(data.T, &data.T);
        compensate_humidity(data.H, data.T, &data.H);
        close(fd);
        zsys_debug("Got data from sensor '%s' - T = %" PRId32 ".%02" PRId32 " C,"
                                            "H = %" PRId32 ".%02" PRId32 " %%",
                  th, data.T/100, data.T%100, data.H/100, data.H%100);
        data_p = &data;
        data.broken = false;
    }

    if ((data_p == NULL) || data_p->broken) {
        free(th);
        return NULL;
    }

    // Formulate a response
    if (what[0] == 't') {
        ret = fty_proto_new (FTY_PROTO_METRIC);
        fty_proto_set_value (ret, "%.2f", data_p->T / (float) 100);
        fty_proto_set_unit (ret, "%s", "C");
        fty_proto_set_ttl (ret, TIME_TO_LIVE);

        zsys_debug ("Returning T = %s C", fty_proto_value (ret));
    }
    else {
        ret = fty_proto_new (FTY_PROTO_METRIC);
        fty_proto_set_value (ret, "%.2f", data_p->H / (float) 100);
        fty_proto_set_unit (ret, "%s", "%");
        fty_proto_set_ttl (ret, TIME_TO_LIVE);

        zsys_debug("Returning H = %s %%", fty_proto_value (ret));
    }
    zhash_t *aux = zhash_new ();
    zhash_autofree (aux);
    zhash_insert (aux, "port", th);
    fty_proto_set_aux (ret, &aux);
    free(th);
    return ret;
}

int
main (int argc, char *argv []) {

    const char *endpoint = "ipc://@/malamute";
    const char *addr = (argc == 1) ? "ipc://@/malamute" : argv[1];
    char *fty_log_level = getenv ("FTY_LOG_LEVEL");
    if (fty_log_level && streq (fty_log_level, "LOG_DEBUG"))
        agent_th_verbose = true;

    // Form ID from hostname and agent name
    char xhostname[HOST_NAME_MAX];
    gethostname(xhostname, HOST_NAME_MAX);
    std::string hostname = xhostname;

    bool have_rc3name = false;

    // Phase 0 - init device real paths
    zsys_info ("Phase 0 - init device real paths");

    for (auto &it: devmap) {
        char *patha = realpath (it.first.c_str (), NULL);
        if (!patha) {
            zsys_warning ("Can't get realpath of %s, using %s: %s", it.first.c_str (), it.second.c_str (), strerror (errno));
            zstr_free (&patha);
            continue;
        }
        std::string npath {patha};
        devmap [it.first] = npath;
        zstr_free (&patha);
    }

    // Phase 1 - Get rack controller asset name
    zsys_info ("Phase 1 - Get rack controller asset name");

    // Temporary workaround
    // Try to read from /var/lib/fty/composite-metrics/agent_th
    zsys_info ("Trying to read from '%s' if it exists", HOSTNAME_FILE);

    zfile_t *file = zfile_new (NULL, HOSTNAME_FILE);
    if (file && zfile_input (file) == 0) {
        zsys_info ("state file '%s' for agent_th exists", HOSTNAME_FILE);
        const char *temp = zfile_readln (file);
        if (temp) {
            hostname.assign (temp);
            zsys_info ("state file contains rc3 name '%s'", hostname.c_str ());
            have_rc3name = true;
        }
        else {
            zsys_error ("could not read from '%s'", HOSTNAME_FILE);
        }
        zfile_close (file);
    }
    zfile_destroy (&file);


    if (have_rc3name == false) {
        zsys_info ("Rack controller asset name not read from file -> listening to ASSETS stream for it");

        mlm_client_t *listener = mlm_client_new ();
        if (!listener) {
            zsys_error ("mlm_client_new () failed");
            return EXIT_FAILURE;
        }

        std::string id = std::string(agent.agent_name) + "@" + hostname;

        int rv = mlm_client_connect (listener, addr, 1000, id.c_str ());
        if (rv == -1) {
            mlm_client_destroy (&listener);
            zsys_error (
                    "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                    addr, id.c_str ());
            return EXIT_FAILURE;
        }
        zsys_info ("Connected to '%s'", endpoint);

        rv = mlm_client_set_consumer (listener, FTY_PROTO_STREAM_ASSETS, ".*");
        if (rv == -1) {
            mlm_client_destroy (&listener);
            zsys_error (
                    "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                    FTY_PROTO_STREAM_ASSETS, ".*");
            return EXIT_FAILURE;
        }
        zsys_info ("Subscribed to '%s'", FTY_PROTO_STREAM_ASSETS);

        zpoller_t *poller = zpoller_new (mlm_client_msgpipe (listener), NULL);
        if (!poller) {
            mlm_client_destroy (&listener);
            zsys_error ("zpoller_new () failed");
            return EXIT_FAILURE;
        }

        while (!zsys_interrupted && have_rc3name == false) {

            void *which = zpoller_wait (poller, 5000); // timeout in msec

            if (which == NULL) {
                zsys_debug ("which == NULL");
                if (zsys_interrupted) {
                    zsys_warning ("interrupted ... ");
                    break;
                }
                if (zpoller_expired (poller)) {
                    continue;
                }
            }

            assert (which == mlm_client_msgpipe (listener));
            zsys_debug ("which == mlm_client_msgpipe");

            zmsg_t *message = mlm_client_recv (listener);
            if (!message) {
                zsys_warning ("message == NULL");
                break;
            }

            fty_proto_t *asset = fty_proto_decode (&message);
            if (!asset || fty_proto_id (asset) != FTY_PROTO_ASSET) {
                fty_proto_destroy (&asset);
                zsys_warning ("fty_proto_decode () failed OR received message not FTY_PROTO_ASSET");
                continue;
            }
            zsys_info ("Received asset message");
            const char *operation = fty_proto_operation (asset);
            const char *type = fty_proto_aux_string (asset, "type", "");
            const char *subtype = fty_proto_aux_string (asset, "subtype", "");

            if ((streq (operation, FTY_PROTO_ASSET_OP_CREATE) || streq (operation, FTY_PROTO_ASSET_OP_UPDATE)) &&
                streq (type, "device") &&
                streq (subtype, "rack controller"))
            {
                hostname = fty_proto_name (asset);
                have_rc3name = true;
                zsys_info ("Received rc3 name '%s'", hostname.c_str ());
                {
                    // Temporary workaround
                    // Try to write to /var/lib/fty/composite-metrics/agent_th

                    zsys_info ("Trying to write to '%s'", HOSTNAME_FILE);
                    file = zfile_new (NULL, HOSTNAME_FILE);
                    if (file) {
                        zfile_remove (file);
                        if (zfile_output (file) == 0) {
                            zchunk_t *chunk = zchunk_new ((const void *) hostname.c_str (), hostname.size ());
                            int rv = zfile_write (file, chunk, (off_t) 0);
                            if (rv != 0)
                                zsys_error ("could not write to '%s'", HOSTNAME_FILE);
                            zchunk_destroy (&chunk);
                            zfile_close (file);
                        }
                        else
                            zsys_error ("'%s' is not writable", HOSTNAME_FILE);
                    }
                    else {
                        zsys_error ("could not write to '%s'", HOSTNAME_FILE);
                    }
                    zfile_destroy (&file);
                }
            }
            fty_proto_destroy (&asset);
        }

        zpoller_destroy (&poller);
        mlm_client_destroy (&listener);
    }
    else {
        zsys_info ("Rack controller asset name read from file.");
    }

    if (zsys_interrupted) {
        // no reason to go further
        return 0;
    }

    // Phase 2
    zsys_info ("Phase 2 - Read sensor data and publish");

    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        zsys_error ("mlm_client_new () failed");
        return EXIT_FAILURE;
    }
    if (getenv ("FTY_LOG_LEVEL") &&  streq (getenv ("FTY_LOG_LEVEL"), "LOG_DEBUG")) {
        zsys_debug ("mlm_client_set_verbose");
        mlm_client_set_verbose (client, true);
    }

    std::string id = std::string(agent.agent_name) + "@" + hostname;

    int rv = mlm_client_connect (client, addr, 1000, id.c_str ());
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                addr, id.c_str ());
        return EXIT_FAILURE;
    }
    zsys_info ("Connected to '%s'", endpoint);

    rv = mlm_client_set_producer (client, FTY_PROTO_STREAM_METRICS_SENSOR);
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_set_producer (stream = '%s') failed",
                FTY_PROTO_STREAM_METRICS_SENSOR);
        return EXIT_FAILURE;
    }
    zsys_info ("Publishing to '%s' as '%s'", FTY_PROTO_STREAM_METRICS_SENSOR, id.c_str());

    while (!zsys_interrupted) {
        // Go through all the stuff we monitor
        char **what = agent.variants;
        while (what != NULL && *what != NULL && !zsys_interrupted) {

            fty_proto_t* msg = agent.get_measurement(*what);
            if (zsys_interrupted) {
                fty_proto_destroy (&msg);
                zsys_warning ("interrupted inner ...  ");
                break;
            }

            if (msg == NULL) {
                zclock_sleep (100);
                what++;
                if (zsys_interrupted) {
                    fty_proto_destroy (&msg);
                    zsys_warning ("interrupted inner ... ");
                    break;
                }
                continue;
            }

/*
--------------------------------------------------------------------------------
stream=_METRICS_SENSOR
sender=agent-th@IPC
subject=temperature.TH1@IPC
D: 17-02-23 14:29:12 BIOS_PROTO_METRIC:
D: 17-02-23 14:29:12     aux=
D: 17-02-23 14:29:12         port=TH1
D: 17-02-23 14:29:12     type='temperature.TH1'
D: 17-02-23 14:29:12     element_src='IPC'
D: 17-02-23 14:29:12     value='24.16'
D: 17-02-23 14:29:12     unit='C'
D: 17-02-23 14:29:12     ttl=300
--------------------------------------------------------------------------------
*/            
            fty_proto_set_name (msg, "%s", hostname.c_str ());

            // TODO: make some hashmap instead
            // type="temperature./dev/sda10"
            // port="/dev/sda10"
            std::string type {*what};
            auto dot_i = type.find ('.');
            std::string port {"/dev/ttyS"};
            port.append (type.substr (dot_i+1, 3));
            type.replace (dot_i + 1, devmap [port].size (), devmap [port]);

            fty_proto_set_type (msg, "%s", type.c_str ());

            // subject temperature./dev/sda10@IPC
            std::string subject {type};
            type.append ("@").append (hostname);

            // Send it
            zmsg_t *to_send = fty_proto_encode (&msg);
            rv = mlm_client_send (client, subject.c_str (), &to_send);
            if (rv != 0) {
                zsys_error ("mlm_client_send (subject = '%s') failed", subject.c_str ());
            }

            fty_proto_destroy (&msg);
            what++;
        }
        if (zsys_interrupted) {
            zsys_warning ("interrupted ...  ");
            break;
        }
        zclock_sleep(POLLING_INTERVAL); // monitoring interval
    }

    mlm_client_destroy (&client);
    return 0;
}
