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
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "libirecovery.h"

#define BUFFER_SIZE 0x1000
#define debug(...) if(device->debug) fprintf(stderr, __VA_ARGS__)

const char* irecv_error_invalid_input     = "Invalid input";
const char* irecv_error_unknown           = "Unknown error";
const char* irecv_error_file_not_found    = "Unable to find file";
const char* irecv_error_usb_status        = "Invalid device status";
const char* irecv_error_no_device         = "Unable to find device";
const char* irecv_error_out_of_memory     = "Unable to allocate memory";
const char* irecv_error_unable_to_connect = "Unable to connect to device";
const char* irecv_error_usb_interface     = "Unable to set device interface";
const char* irecv_error_success           = "Command completed successfully";
const char* irecv_error_usb_upload        = "Unable to upload data to device";
const char* irecv_error_usb_configuration = "Unable to set device configuration";

int irecv_default_sender(irecv_device_t* device, unsigned char* data, int size);
int irecv_default_receiver(irecv_device_t* device, unsigned char* data, int size);

irecv_device_t* irecv_init() {
	struct libusb_context* usb_context = NULL;

	libusb_init(&usb_context);
	irecv_device_t* device = (irecv_device_t*) malloc(sizeof(irecv_device_t));
	if (device == NULL) {
		return NULL;
	}
	memset(device, '\0', sizeof(irecv_device_t));

	irecv_set_receiver(device, &irecv_default_receiver);
	irecv_set_sender(device, &irecv_default_sender);
	device->context = usb_context;
	return device;
}

irecv_error_t irecv_open(irecv_device_t* device) {
	int i = 0;
	int usb_device_count = 0;
	struct libusb_device* usb_device = NULL;
	struct libusb_device** usb_device_list = NULL;
	struct libusb_device_handle* usb_handle = NULL;
	struct libusb_device_descriptor usb_descriptor;

	if (device == NULL || device->context == NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	irecv_error_t error = 0;
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
			
			device->handle = usb_handle;
			device->mode = (irecv_mode_t) usb_descriptor.idProduct;
			error = irecv_set_configuration(device, 1);
			if(error != IRECV_SUCCESS) {
				return error;
			}
			
			error = irecv_set_interface(device, 1, 1);
			if(error != IRECV_SUCCESS) {
				return error;
			}
			
			return IRECV_SUCCESS;
		}
	}

	return IRECV_ERROR_NO_DEVICE;
}

irecv_error_t irecv_set_configuration(irecv_device_t* device, int configuration) {
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	int current = 0;
	libusb_get_configuration(device->handle, &current);
	if(current != configuration) {
		if (libusb_set_configuration(device->handle, configuration) < 0) {
			return IRECV_ERROR_USB_CONFIGURATION;
		}
	}
	
	device->config = configuration;
	return IRECV_SUCCESS;
}

irecv_error_t irecv_set_interface(irecv_device_t* device, int interface, int alt_interface) {
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	if (libusb_claim_interface(device->handle, interface) < 0) {
		return IRECV_ERROR_USB_INTERFACE;
	}
	
	if(alt_interface > 0) {
		if(libusb_set_interface_alt_setting(device->handle, interface, alt_interface) < 0) {
			return IRECV_ERROR_USB_INTERFACE;
		}
	}
	
	device->interface = interface;
	device->alt_interface = alt_interface;
	return IRECV_SUCCESS;
}

irecv_error_t irecv_reset(irecv_device_t* device) {
	if (device == NULL || device->handle != NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	libusb_reset_device(device->handle);
	return IRECV_SUCCESS;
}

irecv_error_t irecv_close(irecv_device_t* device) {
	if (device == NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	if(device->handle != NULL) {
		libusb_release_interface(device->handle, 0);
		libusb_release_interface(device->handle, 1);
		libusb_close(device->handle);
		device->handle = NULL;
	}
	
	return IRECV_SUCCESS;
}

irecv_error_t irecv_exit(irecv_device_t* device) {
	if (device != NULL) {
		if (device->handle != NULL) {
			irecv_close(device);
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

irecv_error_t irecv_set_debug(irecv_device_t* device, int level) {
	if(device == NULL || device->context == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	libusb_set_debug(device->context, level);
	device->debug = level;
	return IRECV_SUCCESS;
}

irecv_error_t irecv_send_command(irecv_device_t* device, unsigned char* command) {
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE;
	}

	unsigned int length = strlen(command);
	if(length >= 0x100) {
		length = 0xFF;
	}
	
	if(device->send_callback != NULL) {
		// Call our user defined callback first, this must return a number of bytes to send
		//   or zero to abort send.
		length = device->send_callback(device, command, length);
	}

	if(length > 0) {
		libusb_control_transfer(device->handle, 0x40, 0, 0, 0, command, length+1, 100);
	}
	
	return IRECV_SUCCESS;
}

irecv_error_t irecv_send_file(irecv_device_t* device, const char* filename) {
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
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

irecv_error_t irecv_get_status(irecv_device_t* device, unsigned int* status) {
	if(device == NULL || device->handle == NULL) {
		*status = 0;
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	unsigned char buffer[6];
	memset(buffer, '\0', 6);
	if(libusb_control_transfer(device->handle, 0xA1, 3, 0, 0, buffer, 6, 500) != 6) {
		*status = 0;
		return IRECV_ERROR_USB_STATUS;
	}
	
	*status = (unsigned int) buffer[4];
	return IRECV_SUCCESS;
}

irecv_error_t irecv_send_buffer(irecv_device_t* device, unsigned char* buffer, unsigned int length) {
	irecv_error_t error = 0;
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	int last = length % 0x800;
	int packets = length / 0x800;
	if (last != 0) {
		packets++;
	} else {
		last = 0x800;
	}

	int i = 0;
	unsigned int status = 0;
	for (i = 0; i < packets; i++) {
		int size = i + 1 < packets ? 0x800 : last;
		int bytes = libusb_control_transfer(device->handle, 0x21, 1, i, 0, &buffer[i * 0x800], size, 500);
		if (bytes != size) {
			free(buffer);
			return IRECV_ERROR_USB_UPLOAD;
		}
		
		error = irecv_get_status(device, &status);
		if (error != IRECV_SUCCESS || status != 5) {
			free(buffer);
			return error;
		}
	}

	libusb_control_transfer(device->handle, 0x21, 1, i, 0, buffer, 0, 1000);
	for (i = 0; i < 3; i++) {
		error = irecv_get_status(device, &status);
		if(error != IRECV_SUCCESS) {
			free(buffer);
			return error;
		}
	}

	free(buffer);
	return IRECV_SUCCESS;
}

irecv_error_t irecv_update(irecv_device_t* device) {
	unsigned char buffer[BUFFER_SIZE];
	memset(buffer, '\0', BUFFER_SIZE);
	if(device == NULL || device->handle == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}

	int bytes = 0;
	while(libusb_bulk_transfer(device->handle, 0x81, buffer, BUFFER_SIZE, &bytes, 100) == 0) {
		if(bytes > 0) {
			if(device->receive_callback != NULL) {
				if(device->receive_callback(device, buffer, bytes) != bytes) {
					return IRECV_ERROR_UNKNOWN;
				}
			}
		} else break;
	}
	
	return IRECV_SUCCESS;
}

int irecv_default_sender(irecv_device_t* device, unsigned char* data, int size) {
	return size;
}

int irecv_default_receiver(irecv_device_t* device, unsigned char* data, int size) {
	int i = 0;
	for(i = 0; i < size; i++) {
		printf("%c", data[i]);
	}
	return size;
}

irecv_error_t irecv_set_receiver(irecv_device_t* device, irecv_receive_callback callback) {
	if(device == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	device->receive_callback = callback;
	return IRECV_SUCCESS;
}

irecv_error_t irecv_set_sender(irecv_device_t* device, irecv_send_callback callback) {
	if(device == NULL) {
		return IRECV_ERROR_NO_DEVICE; 
	}
	
	device->send_callback = callback;
	return IRECV_SUCCESS;
}

const char* irecv_strerror(irecv_error_t error) {
	switch(error) {
	case IRECV_SUCCESS:
		return irecv_error_success;
		
	case IRECV_ERROR_NO_DEVICE:
		return irecv_error_no_device;
		
	case IRECV_ERROR_OUT_OF_MEMORY:
		return irecv_error_out_of_memory;
		
	case IRECV_ERROR_UNABLE_TO_CONNECT:
		return irecv_error_unable_to_connect;
		
	case IRECV_ERROR_INVALID_INPUT:
		return irecv_error_invalid_input;
		
	case IRECV_ERROR_UNKNOWN:
		return irecv_error_unknown;
		
	case IRECV_ERROR_FILE_NOT_FOUND:
		return irecv_error_file_not_found;
		
	case IRECV_ERROR_USB_UPLOAD:
		return irecv_error_usb_upload;
		
	case IRECV_ERROR_USB_STATUS:
		return irecv_error_usb_status;
		
	case IRECV_ERROR_USB_INTERFACE:
		return irecv_error_usb_interface;
		
	case IRECV_ERROR_USB_CONFIGURATION:
		return irecv_error_usb_configuration;
		
	default:
		return irecv_error_unknown;
	}
	
	return NULL;
}
