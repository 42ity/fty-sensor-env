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

#include "agent_th_classes.h"

#include <vector>
#include <map>
#include <string>
#include <cmath>

// TODO: Make the following 5 values configurable
#define NUT_MEASUREMENT_REPEAT_AFTER    300     //!< (once in 5 minutes now (300s))
#define NUT_INVENTORY_REPEAT_AFTER      3600    //!< (every hour now (3600s))
#define NUT_POLLING_INTERVAL            5000    //!< (check with upsd ever 5s)

// Note !!! If you change this value you have to change the following tests as well: TODO
#define AGENT_NUT_REPEAT_INTERVAL_SEC       NUT_MEASUREMENT_REPEAT_AFTER     //<! TODO 
#define AGENT_NUT_SAMPLING_INTERVAL_SEC     5   //!< TODO: We might not need this anymore

#define str(x) #x
#define DEFAULT_LOG_LEVEL LOG_WARNING

bios_proto_t*
get_measurement (char* what);

//typedef bios_proto_t* (get_measurement_fn) (char *what);

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
    int (*init)();            //!< Constructor 
    int (*close)();           //!< Destructor
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
    NULL,
    NULL,
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
get_measurement (char* what)
{
    static std::map<std::string, c_item> cache;
    bios_proto_t* ret = NULL;
    char *th = strdup(what + (strlen(what) - 3));
    c_item data = { 0, false, 0, 0 }, *data_p = NULL;
    uint64_t TTL = 5*60; // 5 minutes time to live for all measurements
    log_debug ("Measuring %s", what);

    // Get data from cache and maybe update the cache
    auto it = cache.find(th);
    if((it == cache.end()) || (time(NULL) - it->second.time) > (NUT_POLLING_INTERVAL/1000)) {
        log_debug("No usable value in cache");
        std::string path = "/dev/ttyS";
    	switch (th[2]) {
           case '1': path +=  "9"; break;
           case '2': path += "10"; break;
           case '3': path += "11"; break;
           case '4': path += "12"; break;
        }
        log_debug("Reading from %s", path.c_str());
        data.time = time(NULL);
        int fd = open_device(path.c_str());
        if(!device_connected(fd)) {
            if(fd > 0)
                close(fd);
            log_debug("No sensor attached to %s", path.c_str());
            data.broken = true;
        }
        else {
            reset_device(fd);
            data.T = get_th_data(fd, MEASURE_TEMP);
            data.H = get_th_data(fd, MEASURE_HUMI);
            compensate_temp(data.T, &data.T);
            compensate_humidity(data.H, data.T, &data.H);
            close(fd);
            log_debug("Got data from sensor %s - T = %" PRId32 ".%02" PRId32 " C,"
                                                "H = %" PRId32 ".%02" PRId32 " %%",
                      th, data.T/100, data.T%100, data.H/100, data.H%100);
            data_p = &data;
            data.broken = false;
        }

        if (it != cache.end()) {
            log_debug("Updating data in cache");
            it->second = data;
        }
        else {
            log_debug("Putting data into cache");
            cache.insert(std::make_pair(th, data));
        }
    }
    else {
        log_debug("Got usable data from cache");
        data_p = &(it->second);
    }

    free(th);
    if ((data_p == NULL) || data_p->broken)
        return NULL;

    // Formulate a response
    if (what[0] == 't') {
        ret = bios_proto_new (BIOS_PROTO_METRIC);
//        bios_proto_set_type ();
//        bios_proto_set_element_src ();
        bios_proto_set_value (ret, "%s", std::to_string (data_p->T/100).c_str ());
        bios_proto_set_unit (ret, "%s", "C");
        bios_proto_set_ttl (ret, TTL);

        /* WIP: keep until tested
        ret = bios_measurement_encode(
                "", // device_name
                "", // quantity
                "C", // units
                data_p->T, // value
                -2, // scale
                TTL); // time
         */
        log_debug("Returning T = %" PRId32 ".%02" PRId32 " C",
                  data_p->T/100, data_p->T%100);
    }
    else {
        ret = bios_proto_new (BIOS_PROTO_METRIC);
        bios_proto_set_value (ret, "%s", std::to_string (data_p->H/100).c_str ());
        bios_proto_set_unit (ret, "%s", "%");
        bios_proto_set_ttl (ret, TTL);

        /* WIP: keep until tested
        ret = bios_measurement_encode(
                "", // device_name
                "", // quantity
                "%", // units
                data_p->H, // value
                -2, // scale
                TTL); // time
        */

        log_debug("Returning H = %" PRId32 ".%02" PRId32 " %%",
                  data_p->H/100, data_p->H%100);
    }
    return ret;
}


void usage () {
    puts ("bios-agent-th [endpoint]\n");
}

int get_log_level (const char *level) {
    if (streq (level, str(LOG_DEBUG))) {
        return LOG_DEBUG;
    }
    else
    if (streq (level, str(LOG_INFO))) {
        return LOG_INFO;
    }
    else
    if (streq (level, str(LOG_WARNING))) {
        return LOG_WARNING;
    }
    else
    if (streq (level, str(LOG_ERR))) {
        return LOG_ERR;
    }
    else
    if (streq (level, str(LOG_CRIT))) {
        return LOG_CRIT;
    }
    return -1;
}

int
main (int argc, char *argv []) {

    if(agent.init != NULL && agent.init())
        return -1;

    const char *endpoint = "ipc://@/malamute";
    int log_level = -1;

    char *env_log_level = getenv ("BIOS_LOG_LEVEL");
    if (env_log_level) {
        log_level = get_log_level (env_log_level);
        if (log_level == -1)
            log_level = DEFAULT_LOG_LEVEL;
    }
    else {
        log_level = DEFAULT_LOG_LEVEL;
    }
    log_set_level (log_level);

    std::map<std::string, std::pair<int32_t, time_t>> cache;

    // Form ID from hostname and agent name
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    std::string id = std::string(agent.agent_name) + "@" + hostname;


    mlm_client_t *client = mlm_client_new ();
    if (!client) {
        log_critical ("mlm_client_new () failed");
        return EXIT_FAILURE;
    }

    int rv = mlm_client_connect (client, endpoint, 1000, id.c_str ());
    if (rv == -1) {
        mlm_client_destroy (&client);
        log_error (
                "mlm_client_connect (endpoint = '%s', timeout = '1000', address = '%s') failed",
                endpoint, id.c_str ());
        return EXIT_FAILURE; 
    }
    log_debug ("Connected to %s", endpoint);

    rv = mlm_client_set_producer (client, BIOS_PROTO_STREAM_METRICS_SENSOR);
    if (rv == -1) {
        mlm_client_destroy (&client);
        log_error (
                "mlm_client_set_producer (stream = '%s') failed",
                BIOS_PROTO_STREAM_METRICS_SENSOR);
        return EXIT_FAILURE;
    }
    log_debug ("Publishing to %s as %s", BIOS_PROTO_STREAM_METRICS_SENSOR, id.c_str());

    while (!zsys_interrupted) {
        // Go through all the stuff we monitor
        char **what = agent.variants;
        while(what != NULL && *what != NULL && !zsys_interrupted) {
            // Get measurement
            bios_proto_t *msg = agent.get_measurement (*what);
            if(msg == NULL) {
                zclock_sleep (100);
                what++;
                continue;
            }
            // Check cache to see if updated value needs to be send
            auto cit = cache.find(*what);
            int32_t int32 = (int32_t) std::trunc (std::stof (bios_proto_value (msg)));
            if((cit == cache.end()) ||
               (abs(cit->second.first -  int32 > agent.diff) ||
               (time(NULL) - cit->second.second > AGENT_NUT_REPEAT_INTERVAL_SEC))) {

                // Prepare topic from templates
                char* topic = (char*)malloc(strlen(agent.at) +
                                            strlen(agent.measurement) +
                                            strlen(hostname) +
                                            strlen(*what) + 5);
                sprintf(topic, agent.at, hostname);
                // ymsg_set_string(msg, "device", topic);
                bios_proto_set_element_src (msg, "%s", topic);

                sprintf(topic, agent.measurement, *what);
                // ymsg_set_string(msg, "quantity", topic);
                bios_proto_set_element_src (msg, "%s", topic);

                strcat(topic, "@");
                sprintf(topic + strlen(topic), agent.at, hostname);
                log_debug ("Sending new measurement '%s' with value '%s'", topic, bios_proto_value (msg));

                // Put it in the cache
                if (cit == cache.end()) {
                    cache.insert(std::make_pair(*what,
                        std::make_pair (int32, time (NULL))));
                }
                else {
                    cit->second.first = int32;
                    cit->second.second = time(NULL);
                }

                // Send it
                zmsg_t *to_send = bios_proto_encode (&msg);  
                rv = mlm_client_send (client, topic, &to_send);
                if (rv != 0) {
                    log_error ("mlm_client_send (subject = '%s')", topic);
                }
                //bios_agent_send(client, topic, &msg);
                zclock_sleep (100);
            }
            else {
                bios_proto_destroy (&msg);
            }
            what++;
        }
        // Hardcoded monitoring interval
        zclock_sleep(NUT_POLLING_INTERVAL);
    }

    mlm_client_destroy (&client);
    if(agent.close != NULL && agent.close())
        return -1;
    return 0;
}
