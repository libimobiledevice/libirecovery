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

typedef enum {
	IRECV_SUCCESS =                   0,
	IRECV_ERROR_NO_DEVICE =          -1,
	IRECV_ERROR_OUT_OF_MEMORY =      -2,
	IRECV_ERROR_UNABLE_TO_CONNECT =  -3,
	IRECV_ERROR_INVALID_INPUT =      -4,
	IRECV_ERROR_UNKNOWN =            -5,
	IRECV_ERROR_FILE_NOT_FOUND =     -6,
	IRECV_ERROR_USB_UPLOAD =         -7,
	IRECV_ERROR_USB_STATUS =         -8,
	IRECV_ERROR_USB_INTERFACE =      -9,
	IRECV_ERROR_USB_CONFIGURATION = -10
} irecv_error_t;

typedef enum {
	kAppleId       = 0x05AC,
	kKernelMode    = 0x1294,
	kRecoveryMode  = 0x1281,
	kDfuMode       = 0x1227
} irecv_mode_t;

struct irecv_device;
typedef struct irecv_device irecv_device_t;

typedef int(*irecv_send_callback)(irecv_device_t* device, unsigned char* data, int size);
typedef int(*irecv_receive_callback)(irecv_device_t* device, unsigned char* data, int size);

struct irecv_device {
	int debug;
	int config;
	int interface;
	int alt_interface;
	irecv_mode_t mode;
	libusb_context* context;
	libusb_device_handle* handle;
	irecv_send_callback send_callback;
	irecv_receive_callback receive_callback;
};

irecv_device_t* irecv_init();
const char* irecv_strerror(irecv_error_t error);
irecv_error_t irecv_open(irecv_device_t* device);
irecv_error_t irecv_exit(irecv_device_t* device);
irecv_error_t irecv_reset(irecv_device_t* device);
irecv_error_t irecv_close(irecv_device_t* device);
irecv_error_t irecv_update(irecv_device_t* device);
irecv_error_t irecv_set_debug(irecv_device_t* device, int level);
irecv_error_t irecv_send_file(irecv_device_t* device, const char* filename);
irecv_error_t irecv_send_command(irecv_device_t* device, unsigned char* command);
irecv_error_t irecv_set_configuration(irecv_device_t* device, int configuration);
irecv_error_t irecv_set_sender(irecv_device_t* device, irecv_send_callback callback);
irecv_error_t irecv_set_receiver(irecv_device_t* device, irecv_receive_callback callback);
irecv_error_t irecv_set_interface(irecv_device_t* device, int interface, int alt_interface);
irecv_error_t irecv_send_buffer(irecv_device_t* device, unsigned char* buffer, unsigned int length);

