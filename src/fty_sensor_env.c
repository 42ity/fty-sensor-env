/*  =========================================================================
    fty_sensor_env - Runs fty-sensor-env-server class

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

/*!
 * \file fty_sensor_env.cc
 * \author Michal Hrusecky <MichalHrusecky@Eaton.com>
 * \author Tomas Halman <TomasHalman@Eaton.com>
 * \author Jim Klimov <EvgenyKlimov@Eaton.com>
 * \author Jiri Kukacka <JiriKukacka@Eaton.com>
 * \brief Not yet documented file
 */
/*
@header
    fty_sensor_env - Runs fty-sensor-env-server class
@discuss
@end
*/

#include <signal.h>

#include "fty_sensor_env_classes.h"

static const char *ACTOR_NAME = "fty-sensor-env";
static const char *ENDPOINT = "ipc://@/malamute";

static const char *config_log = "/etc/fty/ftylog.cfg";

static void s_signal_handler (int signal_value)
{
    log_info("Signal handler triggered");
    s_interrupted = 1;
}

static void s_catch_signals (void)
{
    struct sigaction action;
    log_info("Setting signal handler");
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
}

int main (int argc, char *argv [])
{
    bool verbose = false;
    int argn;

    s_catch_signals();

    for (argn = 1; argn < argc; argn++) {
        const char *param = NULL;
        if (argn < argc - 1) param = argv [argn+1];

        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("fty-sensor-env [options] ...");
            puts ("  --verbose / -v         verbose test output");
            puts ("  --help / -h            this information");
            puts ("  --endpoint / -e        malamute endpoint [ipc://@/malamute]");
            puts ("  --config / -c          config file for logging");
            return 0;
        }
        else if (streq (argv [argn], "--verbose") || streq (argv [argn], "-v")) {
            verbose = true;
        }
        else if (streq (argv [argn], "--endpoint") || streq (argv [argn], "-e")) {
            if (param) ENDPOINT = param;
            ++argn;
        }
        else if (streq (argv [argn], "--config") || streq (argv [argn], "-c")) {
            if (param) config_log = param;
            ++argn;
        }
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }
    s_catch_signals();

    ftylog_setInstance (ACTOR_NAME, config_log);
    Ftylog *log = ftylog_getInstance ();

    if (verbose == true) {
        ftylog_setVeboseMode (log);
        log_info ("fty_sensor_env - started");
    }

    zactor_t *server = zactor_new (sensor_env_actor, NULL);
    s_catch_signals();
    assert (server);
    zstr_sendx (server, "BIND", ENDPOINT, ACTOR_NAME, NULL);
    zstr_sendx (server, "PRODUCER", FTY_PROTO_STREAM_METRICS_SENSOR, NULL);
    zstr_sendx (server, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", NULL);
    zstr_sendx (server, "ASKFORASSETS", NULL);

    while (!s_interrupted) {
        zmsg_t *msg = zactor_recv (server);
        zmsg_destroy (&msg);
    }
    log_info("main: about to quit");
    zactor_destroy (&server);

    log_info ("fty_sensor_env - exited");

    ftylog_delete (log);

    return 0;
}
