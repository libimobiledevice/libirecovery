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
	IRECV_E_SUCCESS =             0,
	IRECV_E_NO_DEVICE =          -1,
	IRECV_E_OUT_OF_MEMORY =      -2,
	IRECV_E_UNABLE_TO_CONNECT =  -3,
	IRECV_E_INVALID_INPUT =      -4,
	IRECV_E_UNKNOWN =            -5,
	IRECV_E_FILE_NOT_FOUND =     -6,
	IRECV_E_USB_UPLOAD =         -7,
	IRECV_E_USB_STATUS =         -8,
	IRECV_E_USB_INTERFACE =      -9,
	IRECV_E_USB_CONFIGURATION =  -10
} irecv_error_t;

#define APPLE_VENDOR_ID 0x05AC

typedef enum {
	kRecoveryMode1 = 0x1280,
	kRecoveryMode2 = 0x1281,
	kRecoveryMode3 = 0x1282,
	kRecoveryMode4 = 0x1283,
	kDfuMode       = 0x1227
} irecv_mode_t;

struct irecv_client;
typedef struct irecv_client* irecv_client_t;

typedef int(*irecv_send_callback)(irecv_client_t client, unsigned char* data, int size);
typedef int(*irecv_receive_callback)(irecv_client_t client, unsigned char* data, int size);

struct irecv_client {
	int debug;
	int config;
	int interface;
	int alt_interface;
	char *uuid;
	irecv_mode_t mode;
	libusb_context* context;
	libusb_device_handle* handle;
	irecv_send_callback send_callback;
	irecv_receive_callback receive_callback;
};

const char* irecv_strerror(irecv_error_t error);
irecv_error_t irecv_open(irecv_client_t* client, const char *uuid);
irecv_error_t irecv_reset(irecv_client_t client);
irecv_error_t irecv_close(irecv_client_t client);
irecv_error_t irecv_receive(irecv_client_t client);
irecv_error_t irecv_set_debug(irecv_client_t client, int level);
irecv_error_t irecv_getenv(irecv_client_t client, unsigned char** var);
irecv_error_t irecv_get_ecid(irecv_client_t client, unsigned long long* pecid);
irecv_error_t irecv_send(irecv_client_t client, unsigned char* command);
irecv_error_t irecv_send_file(irecv_client_t client, const char* filename);
irecv_error_t irecv_send_command(irecv_client_t client, unsigned char* command);
irecv_error_t irecv_set_configuration(irecv_client_t client, int configuration);
irecv_error_t irecv_set_sender(irecv_client_t client, irecv_send_callback callback);
irecv_error_t irecv_set_receiver(irecv_client_t client, irecv_receive_callback callback);
irecv_error_t irecv_set_interface(irecv_client_t client, int interface, int alt_interface);
irecv_error_t irecv_send_buffer(irecv_client_t client, unsigned char* buffer, unsigned int length);

