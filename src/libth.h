/*  =========================================================================
    libth - Internal lib

    Code in this repository is part of Eaton Intelligent Power Controller SW suite                                                        
                                                                                                                                          
    Copyright © 2015-2016 Eaton. This software is confidential and licensed under
    Eaton Proprietary License (EPL or EULA).

    This software is not authorized to be used, duplicated or disclosed to anyone
    without the prior written permission of Eaton.

    Limitations, restrictions and exclusions of the Eaton applicable standard terms
    and conditions, such as its EPL and EULA, apply.
    =========================================================================
*/

#ifndef LIBTH_H_INCLUDED
#define LIBTH_H_INCLUDED

#include "agent_th_library.h"

#define TIOCRS232    0x5201  // OpenGear RS232 setting ioctl
                             //adr  command  r/w
#define STATUS_REG_W 0x06    //000   0011    0
#define STATUS_REG_R 0x07    //000   0011    1
#define MEASURE_TEMP 0x03    //000   0001    1
#define MEASURE_HUMI 0x05    //000   0010    1
#define RESET        0x1e    //000   1111    0


#ifdef __cplusplus
extern "C" {
#endif

//  @interface

AGENT_TH_EXPORT int
    open_device(const char* dev);

AGENT_TH_EXPORT bool
    device_connected(int fd);

AGENT_TH_EXPORT void
    reset_device(int fd);

AGENT_TH_EXPORT int
    get_th_data(int fd, unsigned char what);

AGENT_TH_EXPORT void
    compensate_humidity(int H, int T, int32_t* out);

AGENT_TH_EXPORT void
    compensate_temp(int in, int32_t *out);

//  Self test of this class
AGENT_TH_EXPORT void
    libth_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
