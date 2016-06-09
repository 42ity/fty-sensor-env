/*  =========================================================================
    agent_th - Grab temperature and humidity data from some-sensor

    Code in this repository is part of Eaton Intelligent Power Controller SW suite 
                                                                                   
    Copyright © 2015-2016 Eaton. This software is confidential and licensed under 
    Eaton Proprietary License (EPL or EULA).                                       
                                                                                   
    This software is not authorized to be used, duplicated or disclosed to anyone  
    without the prior written permission of Eaton.                                 
                                                                                   
    Limitations, restrictions and exclusions of the Eaton applicable standard terms
    and conditions, such as its EPL and EULA, apply.                               
    =========================================================================
*/

/*
@header
    agent_th - Grab temperature and humidity data from some-sensor
@discuss
@end
*/

#include "agent_th_classes.h"

int main (int argc, char *argv [])
{
    bool verbose = false;
    int argn;
    for (argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("agent-th [options] ...");
            puts ("  --verbose / -v         verbose test output");
            puts ("  --help / -h            this information");
            return 0;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            verbose = true;
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }
    //  Insert main code here
    if (verbose)
        zsys_info ("agent_th - Grab temperature and humidity data from some-sensor");
    return 0;
}
