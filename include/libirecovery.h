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

typedef struct {
	unsigned int mode;
	struct libusb_context* context;
	struct libusb_device_handle* handle;
} irecv_device;

void irecv_set_debug(int level);
int irecv_open(irecv_device* device);
int irecv_exit(irecv_device* device);
int irecv_init(irecv_device** device);
int irecv_reset(irecv_device* device);
int irecv_close(irecv_device* device);
int irecv_send_file(irecv_device* device, const char* filename);
int irecv_send_command(irecv_device* device, const char* command);
