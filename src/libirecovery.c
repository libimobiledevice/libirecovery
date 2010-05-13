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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#include "libirecovery.h"

static unsigned int irecv_debug = 0;

int irecv_init(irecv_device** p_device) {
	struct libusb_context* usb_context = NULL;

	libusb_init(&usb_context);
	if (irecv_debug) libusb_set_debug(usb_context, 3);

	irecv_device* device = (irecv_device*) malloc(sizeof(irecv_device));
	if (device == NULL) {
		*p_device = NULL;
		return IRECV_ERROR_OUT_OF_MEMORY;
	}
	memset(device, '\0', sizeof(irecv_device));
	device->context = usb_context;

	*p_device = device;
	return IRECV_SUCCESS;
}

int irecv_open(irecv_device* device) {
	int i = 0;
	int usb_device_count = 0;
	struct libusb_device* usb_device = NULL;
	struct libusb_device** usb_device_list = NULL;
	struct libusb_device_handle* usb_handle = NULL;
	struct libusb_device_descriptor usb_descriptor;

	if (device == NULL || device->context == NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	usb_device_count = libusb_get_device_list(device->context, &usb_device_list);
	for (i = 0; i < usb_device_count; i++) {
		usb_device = usb_device_list[i];
		libusb_get_device_descriptor(usb_device, &usb_descriptor);
		if (usb_descriptor.idVendor == kAppleId) {

			libusb_open(usb_device, &usb_handle);
			if (usb_handle == NULL) {
				libusb_free_device_list(usb_device_list, 1);
				return IRECV_ERROR_UNABLE_TO_CONNECT;
			}

			libusb_free_device_list(usb_device_list, 1);
			device->mode = usb_descriptor.idProduct;
			device->handle = usb_handle;
			return IRECV_SUCCESS;
		}
	}

	return IRECV_ERROR_NO_DEVICE;
}

int irecv_reset(irecv_device* device) {
	if (device == NULL || device->handle != NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	libusb_reset_device(device->handle);
	return IRECV_SUCCESS;
}

int irecv_close(irecv_device* device) {
	if (device == NULL || device->handle != NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	libusb_close(device->handle);
	device->handle = NULL;
	return IRECV_SUCCESS;
}

int irecv_exit(irecv_device* device) {
	if (device != NULL) {
		if (device->handle != NULL) {
			libusb_close(device->handle);
			device->handle = NULL;
		}

		if (device->context != NULL) {
			libusb_exit(device->context);
			device->context = NULL;
		}

		free(device);
		device = NULL;
	}

	return IRECV_SUCCESS;
}

void irecv_set_debug(int level) {
	printf("Debug has been set to %d\n", level);
	irecv_debug = level;
}

int irecv_command(irecv_device* device, const char* command) {
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	ssize_t length = strlen(command);
	if(length >= 0x100) {
		return IRECV_ERROR_INVALID_INPUT;
	}

	int ret = libusb_control_transfer(device->handle, 0x40, 0, 0, 0, (unsigned char*) command, length+1, 100);
	if(ret < 0) {
		return IRECV_ERROR_UNKNOWN;
	}

	return IRECV_SUCCESS;
}

int irecv_upload(irecv_device* device, const char* filename) {
	return IRECV_SUCCESS;
}
