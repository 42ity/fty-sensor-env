/*  =========================================================================
    libth - Temperature and humidity lib

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
    =========================================================================
*/

/*
@header
    libth - Temperature and humidity lib
@discuss
@end
*/

/*!
 * \file libth.c
 * \author Michal Hrusecky <MichalHrusecky@Eaton.com>
 * \author Jiri Kukacka <JiriKukacka@Eaton.com>
 * \brief Not yet documented file
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <asm/ioctls.h>
#include <termios.h>
#include <linux/serial.h>
#include <assert.h>

#include "fty_sensor_env_classes.h"

//  --------------------------------------------------------------------------

void compensate_humidity(int H, int T, int32_t* out) {
    double tmp = H;
    // Get linear relative humidity
    tmp = -2.0468 + 0.0367 * tmp - 1.5955E-6 * tmp * tmp;
    // Temperature compensation
    tmp = ((double)T/100 - 25.0)*(0.01 + 0.00008 * (double)H) + tmp;
    *out = tmp * 100;
    return;
}

void compensate_temp(int in, int32_t *out) {
    // Initial compensation for 5V taken from datasheet
    *out = in - 4010;
    return;
}

void msleep(unsigned int m) {
    usleep(m*1000);
}

void set_tx(int fd, int state) {
    if(fd < 0)
        return;
    int what = TIOCM_DTR;
    if(state) {
        ioctl(fd, TIOCMBIS, &what);
    } else {
        ioctl(fd, TIOCMBIC, &what);
    }
}

int get_rx(int fd) {
    int what = 0;
    if(fd < 0)
        return -1;

    ioctl(fd, TIOCMGET, &what);
    return !(what & TIOCM_CTS);
}

void tick(int fd, int state) {
    if(fd < 0)
        return;
    static int last = 1;
    if(state == -1) {
        state = (last + 1) % 2;
    }
    int what = TIOCM_RTS;
    if(state) {
        ioctl(fd, TIOCMBIS, &what);
    } else {
        ioctl(fd, TIOCMBIC, &what);
    }
    last = state;
}

void long_tick(int fd, int state) {
    tick(fd, state);
    msleep(1);
}


/*
 Start sending commands
       _____         ________
 DATA:      |_______|
           ___     ___
 SCK : ___|   |___|   |______
*/

void command_start(int fd) {
    // Init
    set_tx(fd, 1);
    tick(fd, 0);
    // Wait a little with clocks
    long_tick(fd, 0);
    long_tick(fd, 1);
    // Zero for two ticks
    set_tx(fd, 0);
    msleep(1);
    long_tick(fd, 0);
    long_tick(fd, 1);
    // Back to one
    set_tx(fd, 1);
    msleep(1);
    tick(fd, 0);
}


/*
 Reset                                                 (-- start sending --)
       _____________________________________________________         ________
 DATA:                                                      |_______|
          _    _    _    _    _    _    _    _    _        ___     ___
 SCK : __|1|__|2|__|3|__|4|__|5|__|6|__|7|__|8|__|9|______|   |___|   |______

*/

void reset_device(int fd) {
    if(fd < 0)
        return;

    // Init
    set_tx(fd, 1);
    tick(fd, 0);
    // Long 1
    for(int i = 0; i < 9; i++) {
        long_tick(fd, 1);
        long_tick(fd, 0);
    }
    command_start(fd);
}

int read_byte(int fd, unsigned char *val, int ack) {
    set_tx(fd, 1);
    *val = 0;
    for(unsigned char mask = 0x80; mask > 0; mask = mask >> 1) {
        long_tick(fd, 1);
        if(get_rx(fd))
            *val = (*val) | mask;
        long_tick(fd, 0);
    }
    if(ack)
        set_tx(fd, 0);
    else
        set_tx(fd, 1);
    long_tick(fd, 1);
    long_tick(fd, 0);
    set_tx(fd, 1);
    return 0;
}

int write_byte(int fd, unsigned char val) {
    int err = 0;
    for(unsigned char mask = 0x80; mask > 0; mask = mask >> 1) {
        set_tx(fd, val & mask);
        long_tick(fd, 1);
        long_tick(fd, 0);
    }
    set_tx(fd, 1);
    long_tick(fd, 1);
    if(get_rx(fd))
        err = 1;
    long_tick(fd, 0);
    return err;
}

int read_gpi(int fd, int port) {
    int ret = 0;
    if(fd < 0)
        return -1;

    set_tx(fd, 1);
    msleep(1);
    if (-1 == ioctl(fd, TIOCMGET, &ret))
        return -1;
    if (1 == port) {
        ret &= GPI_PORT1_MASK;
        ret >>= GPI_PORT1_BITSHIFT;
    } else if (2 == port) {
        ret &= GPI_PORT2_MASK;
        ret >>= GPI_PORT2_BITSHIFT;
    } else {
        ret = -1;
    }
    return ret;
}

int get_th_data(int fd, unsigned char what) {
    unsigned char tmp[2];
    unsigned char crc;

    if(fd < 0)
        return -1;

    command_start(fd);
    if(write_byte(fd, what))
        return -1;

    msleep(50);

    for(int i=0;i<100000 && get_rx(fd); i++);
        usleep(100);

    if(get_rx(fd))
        return -1;

    read_byte(fd, tmp,   1);
    read_byte(fd, tmp+1, 1);
    read_byte(fd, &crc,  1);
    return ((int)tmp[0])*255 + (int)tmp[1];
}

int device_connected(int fd) {
    int bytes = 0;
    char buf = 'x';
    if(fd < 0)
        return false;
    if(ioctl(fd, TIOCCBRK) < 0)
        goto dev_con_err;
    sleep(1);
    if(ioctl(fd, TIOCSBRK) < 0)
        goto dev_con_err;
    sleep(1);
    if(ioctl(fd, TIOCINQ, &bytes) < 0)
        goto dev_con_err;
    if(bytes > 0 && read(fd, &buf, 1) && buf == '\0')
        return true;
    return false;
dev_con_err:
    close(fd);
    return false;
}

int open_device(const char* dev) {
    int fd = open(dev, O_RDWR);
    if(fd < 0)
        return fd;

    // Set connection parameters
    ioctl(fd, TIOCRS232);
    struct termios termios_s;
    tcgetattr(fd, &termios_s);
    termios_s.c_iflag &= ~(IGNBRK | BRKINT | PARMRK);
    termios_s.c_lflag &= ~ICANON;
    tcsetattr(fd, TCSANOW, &termios_s);

    //Flush all serial port buffers
    tcflush(fd, TCIOFLUSH);

    reset_device(fd);

    return fd;
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
libth_test (bool verbose)
{
    printf (" * libth: ");
    // put some real test here
    printf("Verifying read_gpi fails with invalid file descriptor.\n");
    assert(-1 == read_gpi(-1, 1));
    printf ("OK\n");
}
