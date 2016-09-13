/*  =========================================================================
    libth - Internal lib

    Copyright (C) 2014 - 2015 Eaton

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

#ifndef LIBTH_H_INCLUDED
#define LIBTH_H_INCLUDED

#include "th_library.h"

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

TH_EXPORT int open_device(const char* dev);
TH_EXPORT bool device_connected(int fd);
TH_EXPORT void reset_device(int fd);
TH_EXPORT int get_th_data(int fd, unsigned char what);
TH_EXPORT void compensate_humidity(int H, int T, int32_t* out);
TH_EXPORT void compensate_temp(int in, int32_t *out);

//  @interface
//  @end

#ifdef __cplusplus
}
#endif

#endif
