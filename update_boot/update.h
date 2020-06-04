#ifndef __DEVICE_H_
#define __DEVICE_H_

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "libusb.h"

typedef void* HDEV;

int enum_device(char *dev_list,uint32_t *out_len);
int open_device(char* dev_name,HDEV *device);
int command_transmit(HDEV device,uint8_t *in_buf,uint32_t in_len,uint8_t *out_buf,uint32_t *out_len);
int close_device(HDEV device);

#endif

