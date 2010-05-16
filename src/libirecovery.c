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

irecv_device_t* irecv_init() {
	struct libusb_context* usb_context = NULL;

	libusb_init(&usb_context);
	irecv_device_t* device = (irecv_device_t*) malloc(sizeof(irecv_device_t));
	if (device == NULL) {
		return NULL;
	}
	memset(device, '\0', sizeof(irecv_device_t));
	device->context = usb_context;

	return device;
}

int irecv_open(irecv_device_t* device) {
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

int irecv_reset(irecv_device_t* device) {
	if (device == NULL || device->handle != NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	libusb_reset_device(device->handle);
	return IRECV_SUCCESS;
}

int irecv_close(irecv_device_t* device) {
	if (device == NULL || device->handle != NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	libusb_close(device->handle);
	device->handle = NULL;
	return IRECV_SUCCESS;
}

int irecv_exit(irecv_device_t* device) {
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

void irecv_set_debug(irecv_device_t* device, int level) {
	libusb_set_debug(device->context, level);
	device->debug = level;
}

int irecv_send_command(irecv_device_t* device, unsigned char* command) {
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	ssize_t length = strlen(command);
	if(length >= 0x100) {
		return IRECV_ERROR_INVALID_INPUT;
	}
	
	if(device->send_callback != NULL) {
		// Call our user defined callback first, this must return a number of bytes to send
		//   or zero to abort send.
		length = device->send_callback(device, command, length);
		if(length > 0) {
			int ret = libusb_control_transfer(device->handle, 0x40, 0, 0, 0, (unsigned char*) command, length+1, 100);
			if(ret < 0) {
				return IRECV_ERROR_UNKNOWN;
			}
		}
	}

	return IRECV_SUCCESS;
}

int irecv_send_file(irecv_device_t* device, const char* filename) {
	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		return IRECV_ERROR_FILE_NOT_FOUND;
	}

	fseek(file, 0, SEEK_END);
	int length = ftell(file);
	fseek(file, 0, SEEK_SET);

	unsigned char* buffer = (unsigned char*) malloc(length);
	if (buffer == NULL) {
		fclose(file);
		return IRECV_ERROR_OUT_OF_MEMORY;
	}

	int bytes = fread(buffer, 1, length, file);
	fclose(file);

	if(bytes != length) {
		free(buffer);
		return IRECV_ERROR_UNKNOWN;
	}

	return irecv_send_buffer(device, buffer, length);
}

unsigned int irecv_get_status(irecv_device_t* device) {
	unsigned char status[6];
	memset(status, '\0', 6);
	if(libusb_control_transfer(device->handle, 0xA1, 3, 0, 0, status, 6, 500) != 6) {
		return IRECV_ERROR_USB_STATUS;
	}
	return (unsigned int) status[4];
}

int irecv_send_buffer(irecv_device_t* device, unsigned char* buffer, int length) {
	int last = length % 0x800;
	int packets = length / 0x800;
	if (last != 0) {
		packets++;
	} else {
		last = 0x800;
	}

	int i = 0;
	char status[6];
	for (i = 0; i < packets; i++) {
		int size = i + 1 < packets ? 0x800 : last;
		int bytes = libusb_control_transfer(device->handle, 0x21, 1, i, 0, &buffer[i * 0x800], size, 500);
		if (bytes != size) {
			free(buffer);
			return IRECV_ERROR_USB_UPLOAD;
		}

		if (irecv_get_status(device) != 5) {
			free(buffer);
			return IRECV_ERROR_USB_STATUS;
		}
	}

	libusb_control_transfer(device->handle, 0x21, 1, i, 0, buffer, 0, 1000);
	for (i = 0; i < 3; i++) {
		irecv_get_status(device);
	}

	free(buffer);
	return IRECV_SUCCESS;
}

void irecv_update(irecv_device_t* device) {
	if(device->receive_callback == NULL) {
		return;
	}
}

int irecv_set_receiver(irecv_device_t* device, irecv_receive_callback callback) {
	device->receive_callback = callback;
	return IRECV_SUCCESS;
}

int irecv_set_sender(irecv_device_t* device, irecv_send_callback callback) {
	device->send_callback = callback;
	return IRECV_SUCCESS;
}
