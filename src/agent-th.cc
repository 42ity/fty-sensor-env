/*
Code in this repository is part of Eaton Intelligent Power Controller SW suite

Copyright © 2015-2016 Eaton. This software is confidential and licensed under
Eaton Proprietary License (EPL or EULA).

This software is not authorized to be used, duplicated or disclosed to anyone
without the prior written permission of Eaton.

Limitations, restrictions and exclusions of the Eaton applicable standard terms
and conditions, such as its EPL and EULA, apply.
*/

/*!
 * \file agent-th.cc
 * \author Michal Hrusecky <MichalHrusecky@Eaton.com>
 * \author Tomas Halman <TomasHalman@Eaton.com>
 * \author Jim Klimov <EvgenyKlimov@Eaton.com>
 * \brief Not yet documented file
 */

#include "../include/th_library.h"

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
#define HOSTNAME_FILE "/var/lib/bios/composite-metrics/agent_th"

bios_proto_t*
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

struct sample_agent {
    const char* agent_name;   //!< Name of the measuring agent
    char **variants;          //!< Various sources to iterate over
    const char* measurement;  /*!< Printf formated string for what are we
                                   measuring, %s will be filled with source */
    const char* at;           /*!< Printf formated string for where are we
                                   measuring, %s will be filled with hostname */
    int32_t diff;             /*!< Minimum difference required for publishing */
    bios_proto_t* (*get_measurement)(char* what); //!< Measuring itself
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

bios_proto_t*
get_measurement (char* what) {

    bios_proto_t* ret = NULL;
    char *th = strdup(what + (strlen(what) - 3));
    c_item data = { 0, false, 0, 0 }, *data_p = NULL;    
    zsys_debug ("Measuring '%s'", what);

    std::string path = "/dev/ttyS";
    switch (th[2]) {
       case '1': path +=  "9"; break;
       case '2': path += "10"; break;
       case '3': path += "11"; break;
       case '4': path += "12"; break;
    }
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

    free(th);
    if ((data_p == NULL) || data_p->broken)
        return NULL;

    // Formulate a response
    if (what[0] == 't') {
        ret = bios_proto_new (BIOS_PROTO_METRIC);
        bios_proto_set_value (ret, "%.2f", data_p->T / (float) 100);
        bios_proto_set_unit (ret, "%s", "C");
        bios_proto_set_ttl (ret, TIME_TO_LIVE);

        zsys_debug ("Returning T = %s C", bios_proto_value (ret));
    }
    else {
        ret = bios_proto_new (BIOS_PROTO_METRIC);
        bios_proto_set_value (ret, "%.2f", data_p->H / (float) 100);
        bios_proto_set_unit (ret, "%s", "%");
        bios_proto_set_ttl (ret, TIME_TO_LIVE);

        zsys_debug("Returning H = %s %%", bios_proto_value (ret));
    }
    return ret;
}

int
main (int argc, char *argv []) {

    const char *endpoint = "ipc://@/malamute";
    const char *addr = (argc == 1) ? "ipc://@/malamute" : argv[1];

    char *bios_log_level = getenv ("BIOS_LOG_LEVEL");
    if (bios_log_level && streq (bios_log_level, "LOG_DEBUG"))
        agent_th_verbose = true;

    // Form ID from hostname and agent name
    char xhostname[HOST_NAME_MAX];
    gethostname(xhostname, HOST_NAME_MAX);
    std::string hostname = xhostname;

    // Temporary workaround
    // Try to read from /var/lib/bios/composite-metrics/agent_th
    zfile_t *file = zfile_new (NULL, HOSTNAME_FILE);
    if (file && zfile_input (file) == 0) {
        zsys_debug ("state file for agent_th exists");
        const char *temp = zfile_readln (file);
        if (temp) {
            hostname.assign (temp);
            zsys_debug ("state file contains rc3 name '%s'", hostname.c_str ());
        }
        else
            zsys_error ("could not read from %s", HOSTNAME_FILE);
        zfile_close (file);
    }
    else
        zsys_error ("could not read from %s", HOSTNAME_FILE);
    zfile_destroy (&file);

    std::string id = std::string(agent.agent_name) + "@" + hostname;

    // Create client
    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        zsys_error ("mlm_client_new () failed");
        return EXIT_FAILURE;
    }

    int rv = mlm_client_connect (client, addr, 1000, id.c_str ());
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                addr, id.c_str ());
        return EXIT_FAILURE; 
    }
    zsys_debug ("Connected to %s", endpoint);

    rv = mlm_client_set_consumer (client, BIOS_PROTO_STREAM_ASSETS, ".*");
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_set_consumer (stream = '%s', pattern = '%s') failed",
                BIOS_PROTO_STREAM_ASSETS, ".*");
        return EXIT_FAILURE;
    }
    zsys_debug ("Subscribed to %s", BIOS_PROTO_STREAM_ASSETS);

    rv = mlm_client_set_producer (client, BIOS_PROTO_STREAM_METRICS_SENSOR);
    if (rv == -1) {
        mlm_client_destroy (&client);
        zsys_error (
                "mlm_client_set_producer (stream = '%s') failed",
                BIOS_PROTO_STREAM_METRICS_SENSOR);
        return EXIT_FAILURE;
    }
    zsys_debug ("Publishing to %s as %s", BIOS_PROTO_STREAM_METRICS_SENSOR, id.c_str());

    zpoller_t *poller = zpoller_new (mlm_client_msgpipe (client), NULL);
    if (!poller) {
        zsys_error ("zpoller_new () failed"); 
        return EXIT_FAILURE;
    }

    while (!zsys_interrupted) {
        // Go through all the stuff we monitor
        char **what = agent.variants;
        while(what != NULL && *what != NULL && !zsys_interrupted) {

            bios_proto_t* msg = agent.get_measurement(*what);
            if (zsys_interrupted) {
                bios_proto_destroy (&msg);
                break;
            }

            if (msg == NULL) {
                zclock_sleep (100);
                what++;
                if (zsys_interrupted) {
                    break;
                } 
                continue;
            }

            // Prepare topic from templates
            char* topic = (char*)malloc(strlen(agent.at) +
                                        strlen(agent.measurement) +
                                        hostname.size () +
                                        strlen(*what) + 5);
            sprintf(topic, agent.at, hostname.c_str ());
            bios_proto_set_element_src (msg, "%s", topic);

            sprintf(topic, agent.measurement, *what);
            bios_proto_set_type (msg, "%s", topic);
            strcat(topic, "@");
            sprintf(topic + strlen(topic), agent.at, hostname.c_str ());
            zsys_debug("Sending new measurement '%s' with value '%s'", topic, bios_proto_value (msg));


            // Send it
            zmsg_t *to_send = bios_proto_encode (&msg);  
            rv = mlm_client_send (client, topic, &to_send);
            if (rv != 0) {
                zsys_error ("mlm_client_send (subject = '%s')", topic);
            }

            bios_proto_destroy (&msg);

            
            what++;
        }
        // Hardcoded monitoring interval
        zclock_sleep(POLLING_INTERVAL - 1000);
        void *which = zpoller_wait (poller, 1000); // timeout in msec 
        if (which == mlm_client_msgpipe (client)) {
            zmsg_t *message = mlm_client_recv (client);
            if (!message)
                break;
            bios_proto_t *asset = bios_proto_decode (&message);
            if (!asset || bios_proto_id (asset) != BIOS_PROTO_ASSET) {
                bios_proto_destroy (&asset);
                zsys_warning ("bios_proto_decode () failed OR received message not BIOS_PROTO_ASSET");
                continue;
            }
            const char *operation = bios_proto_operation (asset);
            const char *type = bios_proto_aux_string (asset, "type", "");
            const char *subtype = bios_proto_aux_string (asset, "subtype", "");
            
            if ((streq (operation, BIOS_PROTO_ASSET_OP_CREATE) || streq (operation, BIOS_PROTO_ASSET_OP_UPDATE)) &&
                streq (type, "device") &&
                streq (subtype, "rack controller")) {                
                hostname = bios_proto_name (asset); 
            }
            bios_proto_destroy (&asset);
        }
        else if (!zpoller_expired (poller)) {
            break;
        }
    }
    
    // Temporary workaround
    // Try to write to /var/lib/bios/composite-metrics/agent_th
    file = zfile_new (NULL, HOSTNAME_FILE);
    if (file) {
        zfile_remove (file);
        if (zfile_output (file) == 0) {
            zchunk_t *chunk = zchunk_new ((const void *) hostname.c_str (), hostname.size ());
            rv = zfile_write (file, chunk, (off_t) 0);
            if (rv != 0)
                zsys_error ("could not write to %s", HOSTNAME_FILE);
            zchunk_destroy (&chunk);
            zfile_close (file);
        }
        else 
            zsys_error ("%s is not writable", HOSTNAME_FILE);
    }
    else {
        zsys_error ("could not write to %s", HOSTNAME_FILE);
    }
    zfile_destroy (&file);

    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    return 0;
}
