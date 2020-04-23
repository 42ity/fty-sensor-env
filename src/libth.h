/*  =========================================================================
    libth - Temperature and humidity lib

    Copyright (C) 2014 - 2020 Eaton

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

#define TIOCRS232    0x5201  // OpenGear RS232 setting ioctl
                             //adr  command  r/w
#define STATUS_REG_W 0x06    //000   0011    0
#define STATUS_REG_R 0x07    //000   0011    1
#define MEASURE_TEMP 0x03    //000   0001    1
#define MEASURE_HUMI 0x05    //000   0010    1
#define RESET        0x1e    //000   1111    0

#define GPI_PORT1_BITSHIFT  8
#define GPI_PORT2_BITSHIFT  6
#define GPI_PORT1_MASK      1 << GPI_PORT1_BITSHIFT
#define GPI_PORT2_MASK      1 << GPI_PORT2_BITSHIFT

#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  Create a new libth - not used for library
//FTY_SENSOR_ENV_PRIVATE libth_t *
//    libth_new (void);

//  Destroy the libth - not used for library
//FTY_SENSOR_ENV_PRIVATE void
//    libth_destroy (libth_t **self_p);

//  Self test of this class
FTY_SENSOR_ENV_PRIVATE void
    libth_test (bool verbose);

//  Open device for reading
FTY_SENSOR_ENV_PRIVATE int
    open_device (const char* dev);

//  Check if device is connected
FTY_SENSOR_ENV_PRIVATE int
    device_connected (int fd);

//  Reset connected device
FTY_SENSOR_ENV_PRIVATE void
    reset_device (int fd);

//  Get data from device (temperature, humidity)
FTY_SENSOR_ENV_PRIVATE int
    get_th_data (int fd, unsigned char what);

//  Fix humidity reading
FTY_SENSOR_ENV_PRIVATE void
    compensate_humidity (int H, int T, int32_t* out);

//  Fix temperature reading
FTY_SENSOR_ENV_PRIVATE void
    compensate_temp (int in, int32_t *out);

//  Read GPI from connected device
FTY_SENSOR_ENV_PRIVATE int
    read_gpi(int fd, int port);
//  @end

#ifdef __cplusplus
}
#endif

#endif
