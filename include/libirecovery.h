/**
 * iRecovery - Utility for DFU 2.0, WTF and Recovery Mode
 * Copyright (C) 2008 - 2009 westbaer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <libusb-1.0/libusb.h>

#define IRECV_SUCCESS                   0
#define IRECV_ERROR_NO_DEVICE          -1
#define IRECV_ERROR_OUT_OF_MEMORY      -2
#define IRECV_ERROR_UNABLE_TO_CONNECT  -3
#define IRECV_ERROR_INVALID_INPUT      -4
#define IRECV_ERROR_UNKNOWN            -5
#define IRECV_ERROR_FILE_NOT_FOUND     -6
#define IRECV_ERROR_USB_UPLOAD         -7
#define IRECV_ERROR_USB_STATUS         -8

enum {
	kAppleId       = 0x05AC,
	kKernelMode    = 0x1294,
	kRecoveryMode  = 0x1281,
	kDfuMode       = 0x1227
};

struct irecv_device;
typedef struct irecv_device irecv_device_t;

typedef int(*irecv_send_callback)(irecv_device_t* device, unsigned char* data, unsigned int size);
typedef int(*irecv_receive_callback)(irecv_device_t* device, unsigned char* data, unsigned int size);

struct irecv_device {
	unsigned int mode;
	unsigned int debug;
	struct libusb_context* context;
	struct libusb_device_handle* handle;
	irecv_receive_callback receive_callback;
	irecv_send_callback send_callback;
};

irecv_device_t* irecv_init();
int irecv_open(irecv_device_t* device);
int irecv_exit(irecv_device_t* device);
int irecv_reset(irecv_device_t* device);
int irecv_close(irecv_device_t* device);
void irecv_update(irecv_device_t* device);
void irecv_set_debug(irecv_device_t* device, int level);
int irecv_send_file(irecv_device_t* device, const char* filename);
int irecv_send_command(irecv_device_t* device, unsigned char* command);
int irecv_send_buffer(irecv_device_t* device, unsigned char* buffer, int length);
int irecv_set_sender(irecv_device_t* device, irecv_send_callback callback);
int irecv_set_receiver(irecv_device_t* device, irecv_receive_callback callback);

