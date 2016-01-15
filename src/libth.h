/*  =========================================================================
    libth - Internal lib

    Code in this repository is part of Eaton Intelligent Power Controller SW suite                                                        
                                                                                                                                          
    Copyright (C) 2015 Eaton                                                                                                              
                                                                                                                                          
    The software that accompanies this License is the property of Eaton                                                                   
    and is protected by copyright and other intellectual property law.                                                                    
                                                                                                                                          
    Final content under discussion...                                                                                                     
    Refer to http://pqsoftware.eaton.com/explore/eng/ipp/license_en.htm?lang=en&file=install/win32/ipp/ipp_win_1_42_109.exe&os=WIN&typeOs=
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


#ifdef __cplusplus
extern "C" {
#endif

int open_device(const char* dev);
bool device_connected(int fd);
void reset_device(int fd);
int get_th_data(int fd, unsigned char what);
void compensate_humidity(int H, int T, int32_t* out);
void compensate_temp(int in, int32_t *out);

//  @interface
//  @end

#ifdef __cplusplus
}
#endif

#endif
