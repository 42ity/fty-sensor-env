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
 * \brief Not yet documented file
 */

#include "../include/fty_sensor_env_library.h"
#include "../include/fty_sensor_env.h"

#include <vector>
#include <map>
#include <string>
#include <cmath>

#define POLLING_INTERVAL            5000
#define TIME_TO_LIVE                300
#define AGENT_NAME                  "fty-sensor-env"

// ### logging
bool agent_th_verbose = false;
#define zsys_debug(...) \
    do { if (agent_th_verbose) zsys_debug (__VA_ARGS__); } while (0);

// temporary
#define HOSTNAME_FILE "/var/lib/fty/fty-sensor-env/state"

// strdup is to avoid -Werror=write-strings - not enough time to rewrite it properly
// + it's going to be proprietary code ;-)
char *vars[] = {
    strdup (TEMPERATURE TH1),
    strdup (HUMIDITY TH1),
    strdup (STATUSGPI1 TH1),
    strdup (STATUSGPI2 TH1),
    strdup (TEMPERATURE TH2),
    strdup (HUMIDITY TH2),
    strdup (STATUSGPI1 TH2),
    strdup (STATUSGPI2 TH2),
    strdup (TEMPERATURE TH3),
    strdup (HUMIDITY TH3),
    strdup (STATUSGPI1 TH3),
    strdup (STATUSGPI2 TH3),
    strdup (TEMPERATURE TH4),
    strdup (HUMIDITY TH4),
    strdup (STATUSGPI1 TH4),
    strdup (STATUSGPI2 TH4),
    NULL
};

// maps symbolic name to real device name!!
std::map <std::string, std::string> devmap = {
    {"/dev/ttySTH1", "/dev/ttyS9"},
    {"/dev/ttySTH2", "/dev/ttyS10"},
    {"/dev/ttySTH3", "/dev/ttyS11"},
    {"/dev/ttySTH4", "/dev/ttyS12"}
};

struct c_item {
    time_t time;
    bool broken;
    int32_t T;
    int32_t H;
};

fty_proto_t*
get_measurement (char* what) {

    fty_proto_t* ret = fty_proto_new (FTY_PROTO_METRIC);
    zhash_t *aux = zhash_new ();
    zhash_autofree (aux);
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
        if (0 == strncmp(TEMPERATURE, what, strlen(TEMPERATURE))) {
            data.T = get_th_data(fd, MEASURE_TEMP);
            compensate_temp(data.T, &data.T);
            zsys_debug("Got data from sensor '%s' - T = %" PRId32 ".%02" PRId32 " C", th, data.T/100, data.T%100);

            fty_proto_set_value (ret, "%.2f", data.T / (float) 100);
            fty_proto_set_unit (ret, "%s", "C");
            fty_proto_set_ttl (ret, TIME_TO_LIVE);

            zsys_debug ("Returning T = %s C", fty_proto_value (ret));
        } else if (0 == strncmp(HUMIDITY, what, strlen(HUMIDITY))) {
            data.T = get_th_data(fd, MEASURE_TEMP);
            data.H = get_th_data(fd, MEASURE_HUMI);
            compensate_humidity(data.H, data.T, &data.H);
            zsys_debug("Got data from sensor '%s' - H = %" PRId32 ".%02" PRId32 " %%", th, data.T/100, data.T%100,
                    data.H/100, data.H%100);

            fty_proto_set_value (ret, "%.2f", data.H / (float) 100);
            fty_proto_set_unit (ret, "%s", "%");
            fty_proto_set_ttl (ret, TIME_TO_LIVE);

            zsys_debug ("Returning H = %s %%", fty_proto_value (ret));
        } else if (0 == strncmp(STATUSGPIX, what, strlen(STATUSGPIX))) {
            // read GPI from sensor - expect ports in range 1-9
            char *where = what + strlen(STATUSGPIX);
            char gpi_port = *where - '0';
            int gpi = read_gpi(fd, gpi_port);

            fty_proto_set_value (ret, "%d", gpi);
            fty_proto_set_unit (ret, "%s", "");
            fty_proto_set_ttl (ret, TIME_TO_LIVE);
            zhash_insert (aux, "ext-port", where);

            zsys_debug ("Returning H = %s", fty_proto_value (ret));
        } else {
            free(th);
            fty_proto_destroy (&ret);
            close(fd);
            zhash_destroy (&self->aux);
            return NULL;
        }
        close(fd);
        data_p = &data;
        data.broken = false;
    }

    if ((data_p == NULL) || data_p->broken) {
        free(th);
        fty_proto_destroy (&ret);
        zhash_destroy (&self->aux);
        return NULL;
    }

    zhash_insert (aux, "port", th);
    fty_proto_set_aux (ret, &aux);
    free(th);
    return ret;
}

static bool
s_read_statefile (const std::string& filename, std::string& rc3id)
{
    zsys_info ("Reading RC3 id from state file '%s'.", filename.c_str ());
    zfile_t *file = zfile_new (NULL, filename.c_str ());
    if (!file) {
        zsys_error ("zfile_new (path = 'NULL', name = '%s')", filename.c_str ());
        return false;
    }
    if (zfile_input (file) != 0) {
        zfile_destroy (&file);
        zsys_warning ("State file '%s' not found or not accessible.", filename.c_str ());
        return false;
    }
    zsys_info ("State file '%s' exists and is accessible.", filename.c_str ());
    const char *temp = zfile_readln (file);
    if (!temp) {
        zfile_close (file);
        zfile_destroy (&file);
        zsys_warning ("State file '%s' is empty.", filename.c_str ());
        return false;
    }
    rc3id.assign (temp);
    zsys_info ("RC3 id read from state file == '%s'.", rc3id.c_str ());

    zfile_close (file);
    zfile_destroy (&file);
    return true;
}

static void
s_write_statefile (const std::string& filename, const std::string& rc3id)
{
    zsys_info ("Writing RC3 id '%s' to state file '%s'", rc3id.c_str (), filename.c_str ());
    zfile_t *file = zfile_new (NULL, filename.c_str ());
    if (!file) {
        zsys_error ("zfile_new (path = 'NULL', name = '%s')", filename.c_str ());
        return;
    }

    zfile_remove (file);

    if (zfile_output (file) != 0) {
        zfile_destroy (&file);
        zsys_warning ("State file '%s' not found or not writable.", filename.c_str ());
        return;
    }

    zchunk_t *chunk = zchunk_new ((const void *) rc3id.c_str (), rc3id.size ());
    int rv = zfile_write (file, chunk, (off_t) 0);
    if (rv != 0)
        zsys_error ("Error writing to state file '%s'.", filename.c_str ());
    else
        zsys_info ("Writing RC3 id '%s' to state file '%s' successfull.", rc3id.c_str (), filename.c_str ());
    zchunk_destroy (&chunk);

    zfile_close (file);
    zfile_destroy (&file);
    return;
}

static void
read_sensors (mlm_client_t *client, const char *hostname)
{
    assert (client);
    assert (hostname);
    char **what = vars;

    while (what != NULL && *what != NULL) {
        fty_proto_t* msg = get_measurement (*what);
        if (msg == NULL) {
            zclock_sleep (100);
            what++;
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
        fty_proto_set_name (msg, "%s", hostname);
        fty_proto_set_time (msg, ::time (NULL));

        // TODO: make some hashmap instead
        // type="temperature./dev/sda10"
        // port="/dev/sda10"
        std::string type {*what};
        auto dot_i = type.find_last_of ('.');
        std::string port {"/dev/ttyS"};
        port.append (type.substr (dot_i+1, 3));
        type.replace (dot_i + 1, devmap [port].size (), devmap [port]);

        fty_proto_set_type (msg, "%s", type.c_str ());

        // subject temperature./dev/sda10@IPC
        std::string subject {type};
        subject.append ("@").append (hostname);

        // Send it
        zmsg_t *to_send = fty_proto_encode (&msg);
        int rv = mlm_client_send (client, subject.c_str (), &to_send);
        if (rv != 0) {
            zsys_error ("mlm_client_send (subject = '%s') failed", subject.c_str ());
        }

        fty_proto_destroy (&msg);
        what++;
    }
}

int
main (int argc, char *argv []) {

    const char *endpoint = "ipc://@/malamute";
    const char *addr = (argc == 1) ? "ipc://@/malamute" : argv[1];
    char *fty_log_level = getenv ("BIOS_LOG_LEVEL");
    if (fty_log_level && streq (fty_log_level, "LOG_DEBUG"))
        agent_th_verbose = true;

    std::string hostname;

    bool have_rc3id = false;

    zsys_info ("Initializing device real paths.");
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
    zsys_info ("Device real paths initiated.");

    have_rc3id = s_read_statefile (HOSTNAME_FILE, hostname);

    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        zsys_error ("mlm_client_new () failed");
        return EXIT_FAILURE;
    }

    if (fty_log_level && streq (fty_log_level, "LOG_DEBUG")) {
        zsys_debug ("mlm_client_set_verbose");
        mlm_client_set_verbose (client, true);
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

    uint64_t timestamp = (uint64_t) zclock_mono ();
    uint64_t timeout = (uint64_t) POLLING_INTERVAL;

    while (!zsys_interrupted) {
        zsys_debug ("cycle ... ");
        void *which = zpoller_wait (poller, timeout);

        if (which == NULL) {
            if (zpoller_terminated (poller) || zsys_interrupted)
                break;
            if (zpoller_expired (poller)) {
                if (have_rc3id)
                    read_sensors (client, hostname.c_str ());
            }
            timestamp = (uint64_t) zclock_mono ();
            continue;
        }

        uint64_t now = (uint64_t) zclock_mono ();
        if (now - timestamp >= timeout) {
            if (have_rc3id)
                read_sensors (client, hostname.c_str ());
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

        if (streq (type, "device") &&
            (streq (subtype, "rack controller") || streq (subtype, "rackcontroller" )) )
        {
            if ((streq (operation, FTY_PROTO_ASSET_OP_CREATE)
                || streq (operation, FTY_PROTO_ASSET_OP_UPDATE))
                && have_rc3id == false)
            {
                hostname.assign (name);
                have_rc3id = true;
                zsys_info ("Received rc3 id '%s'.", hostname.c_str ());
                s_write_statefile (HOSTNAME_FILE, hostname);

            }
            else
            if ((streq (operation, FTY_PROTO_ASSET_OP_DELETE)
                || streq (operation, FTY_PROTO_ASSET_OP_RETIRE))
                && have_rc3id == true)
            {
                hostname.assign ("");
                have_rc3id = false;
                s_write_statefile (HOSTNAME_FILE, "");
            }
        }
        fty_proto_destroy (&asset);
    }

    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    return 0;
}
