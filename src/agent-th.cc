
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

#include "libth.h"

// TODO: Make the following 5 values configurable
#define NUT_MEASUREMENT_REPEAT_AFTER    300     //!< (once in 5 minutes now (300s))
#define NUT_INVENTORY_REPEAT_AFTER      3600    //!< (every hour now (3600s))
#define NUT_POLLING_INTERVAL            5000    //!< (check with upsd ever 5s)

// Note !!! If you change this value you have to change the following tests as well: TODO
#define AGENT_NUT_REPEAT_INTERVAL_SEC       NUT_MEASUREMENT_REPEAT_AFTER     //<! TODO 
#define AGENT_NUT_SAMPLING_INTERVAL_SEC     5   //!< TODO: We might not need this anymore

// ### logging
bool agent_th_verbose = false;
#define zsys_debug(...) \
    do { if (agent_th_verbose) zsys_debug (__VA_ARGS__); } while (0);

// ### cleanup
#ifndef __GNUC__
#error Please use a compiler supporting attribute cleanup
#endif

static inline void _destroy_bios_agent (bios_agent_t **self_p) {
    bios_agent_destroy (self_p);
}
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _scoped_bios_agent_t    _cleanup_(_destroy_bios_agent) bios_agent_t

ymsg_t* get_measurement(char* what);

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
    ymsg_t* (*get_measurement)(char* what); //!< Measuring itself
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

ymsg_t* get_measurement(char* what) {
    static std::map<std::string, c_item> cache;
    ymsg_t* ret = NULL;
    char *th = strdup(what + (strlen(what) - 3));
    c_item data = { 0, false, 0, 0 }, *data_p = NULL;

    zsys_debug("Measuring %s", what);

    // Get data from cache and maybe update the cache
    auto it = cache.find(th);
    if((it == cache.end()) || (time(NULL) - it->second.time) > (NUT_POLLING_INTERVAL/1000)) {
        zsys_debug("No usable value in cache");
        std::string path = "/dev/ttyS";
	switch(th[2]) {
           case '1': path +=  "9"; break;
           case '2': path += "10"; break;
           case '3': path += "11"; break;
           case '4': path += "12"; break;
        }
        zsys_debug("Reading from %s", path.c_str());
        data.time = time(NULL);
        int fd = open_device(path.c_str());
        if(!device_connected(fd)) {
            if(fd > 0)
                close(fd);
            zsys_warning("No sensor attached to %s", path.c_str());
            data.broken = true;
        } else {
            reset_device(fd);
            data.T = get_th_data(fd, MEASURE_TEMP);
            data.H = get_th_data(fd, MEASURE_HUMI);
            compensate_temp(data.T, &data.T);
            compensate_humidity(data.H, data.T, &data.H);
            close(fd);
            zsys_debug("Got data from sensor %s - T = %" PRId32 ".%02" PRId32 " C,"
                                                "H = %" PRId32 ".%02" PRId32 " %%",
                      th, data.T/100, data.T%100, data.H/100, data.H%100);
            data_p = &data;
            data.broken = false;
        }
        if(it != cache.end()) {
            zsys_debug("Updating data in cache");
            it->second = data;
        } else {
            zsys_debug("Putting data into cache");
            cache.insert(std::make_pair(th, data));
        }
    } else {
        zsys_debug("Got usable data from cache");
        data_p = &(it->second);
    }

    free(th);
    if((data_p == NULL) || data_p->broken)
        return NULL;

    // Formulate a response
    if(what[0] == 't') {
        ret = bios_measurement_encode("", "", "C", data_p->T, -2, data_p->time);
        zsys_debug("Returning T = %" PRId32 ".%02" PRId32 " C",
                  data_p->T/100, data_p->T%100);
    } else {
        ret = bios_measurement_encode("", "", "%", data_p->H, -2, data_p->time);
        zsys_debug("Returning H = %" PRId32 ".%02" PRId32 " %%",
                  data_p->H/100, data_p->H%100);
    }
    return ret;
}

//sample agent
int main (int argc, char *argv []) {
    // Basic settings
    if (argc > 1) {
        printf ("syntax: %s [ ipc://...|tcp://... ]\n", argv[0]);
        return 0;
    }

    char *bios_log_level = getenv ("BIOS_LOG_LEVEL");
    if (bios_log_level && streq (bios_log_level, "LOG_DEBUG"))
        agent_th_verbose = true;

    const char *addr = (argc == 1) ? "ipc://@/malamute" : argv[1];
    std::map<std::string, std::pair<int32_t, time_t>> cache;

    if(agent.init != NULL && agent.init())
        return -1;

    // Form ID from hostname and agent name
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    std::string id = std::string(agent.agent_name) + "@" + hostname;

    // Create client
    _scoped_bios_agent_t *client = bios_agent_new(addr, id.c_str());
    if(!client) {
        zsys_error("server not reachable at %s", addr);
        exit (EXIT_FAILURE);
    }
    zsys_debug ("Connected to %s", addr);
    bios_agent_set_producer(client, bios_get_stream_main());
    zsys_debug ("Publishing to %s as %s", bios_get_stream_main(), id.c_str());

    // Until interrupted
    while(!zsys_interrupted) {
        // Go through all the stuff we monitor
        char **what = agent.variants;
        while(what != NULL && *what != NULL && !zsys_interrupted) {
            // Get measurement
            ymsg_t* msg = agent.get_measurement(*what);
            if(msg == NULL) {
                zclock_sleep (100);
                what++;
                continue;
            }
            // Check cache to see if updated value needs to be send
            auto cit = cache.find(*what);
            if((cit == cache.end()) ||
               (abs(cit->second.first - ymsg_get_int32(msg, "value")) > agent.diff) ||
               (time(NULL) - cit->second.second > AGENT_NUT_REPEAT_INTERVAL_SEC)) {

                // Prepare topic from templates
                char* topic = (char*)malloc(strlen(agent.at) +
                                            strlen(agent.measurement) +
                                            strlen(hostname) +
                                            strlen(*what) + 5);
                sprintf(topic, agent.at, hostname);
                ymsg_set_string(msg, "device", topic);
                sprintf(topic, agent.measurement, *what);
                ymsg_set_string(msg, "quantity", topic);
                strcat(topic, "@");
                sprintf(topic + strlen(topic), agent.at, hostname);
                zsys_debug("Sending new measurement '%s' with value %" PRIi32 " * 10^%" PRIi32,
                         topic, ymsg_get_int32(msg, "value"),
                                ymsg_get_int32(msg, "scale"));

                // Put it in the cache
                if(cit == cache.end()) {
                    cache.insert(std::make_pair(*what,
                        std::make_pair(ymsg_get_int32(msg, "value"), time(NULL))));
                } else {
                    cit->second.first = ymsg_get_int32(msg, "value");
                    cit->second.second = time(NULL);
                }

                // Send it
                bios_agent_send(client, topic, &msg);

                zclock_sleep (100);
            } else {
                ymsg_destroy(&msg);
            }
            what++;
        }
        // Hardcoded monitoring interval
        zclock_sleep(NUT_POLLING_INTERVAL);
    }

    bios_agent_destroy(&client);
    if(agent.close != NULL && agent.close())
        return -1;
    return 0;
}
