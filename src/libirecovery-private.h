/*
 * Private helpers shared with focused transport tests.
 */
#ifndef LIBIRECOVERY_PRIVATE_H
#define LIBIRECOVERY_PRIVATE_H

#include <stddef.h>
#include <string.h>

#include "libirecovery.h"

static inline int irecv_usb_should_clear_halt(int transfer_result, int pipe_error)
{
	return transfer_result == pipe_error;
}

static const char* const irecv_device_teardown_commands[] = {
	"go",
	"bootx",
	"reboot",
	"memboot"
};

static inline int irecv_command_uses_b_request(const char* command)
{
	size_t i;

	if (command == NULL) {
		return 0;
	}

	for (i = 0; i < sizeof(irecv_device_teardown_commands) / sizeof(irecv_device_teardown_commands[0]); i++) {
		if (strcmp(command, irecv_device_teardown_commands[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

static inline int irecv_command_is_device_teardown(const char* command)
{
	size_t token_length;
	size_t i;

	if (command == NULL) {
		return 0;
	}

	command += strspn(command, " \t\r\n");
	token_length = strcspn(command, " \t\r\n");
	for (i = 0; i < sizeof(irecv_device_teardown_commands) / sizeof(irecv_device_teardown_commands[0]); i++) {
		if (strlen(irecv_device_teardown_commands[i]) == token_length
			&& strncmp(command, irecv_device_teardown_commands[i], token_length) == 0) {
			return 1;
		}
	}

	return 0;
}

static inline int irecv_command_error_is_accepted(irecv_error_t error, const char* command)
{
	if (error == IRECV_E_PIPE) {
		return 1;
	}

	return irecv_command_is_device_teardown(command)
		&& (error == IRECV_E_NO_DEVICE
			|| error == IRECV_E_TIMEOUT
			|| error == IRECV_E_USB_STATUS);
}

static inline irecv_error_t irecv_map_control_transfer_error(int result, int pipe_error, int timeout_error, int no_device_error, int no_memory_error)
{
	if (result == pipe_error) {
		return IRECV_E_PIPE;
	}
	if (result == timeout_error) {
		return IRECV_E_TIMEOUT;
	}
	if (result == no_device_error) {
		return IRECV_E_NO_DEVICE;
	}
	if (result == no_memory_error) {
		return IRECV_E_OUT_OF_MEMORY;
	}
	return IRECV_E_USB_STATUS;
}

static inline irecv_error_t irecv_preserve_control_transfer_error(int result)
{
	switch (result) {
	case IRECV_E_NO_DEVICE:
	case IRECV_E_OUT_OF_MEMORY:
	case IRECV_E_UNABLE_TO_CONNECT:
	case IRECV_E_INVALID_INPUT:
	case IRECV_E_FILE_NOT_FOUND:
	case IRECV_E_USB_UPLOAD:
	case IRECV_E_USB_STATUS:
	case IRECV_E_USB_INTERFACE:
	case IRECV_E_USB_CONFIGURATION:
	case IRECV_E_PIPE:
	case IRECV_E_TIMEOUT:
	case IRECV_E_UNSUPPORTED:
	case IRECV_E_UNKNOWN_ERROR:
		return (irecv_error_t)result;
	default:
		return IRECV_E_USB_STATUS;
	}
}

static inline irecv_error_t irecv_finalize_control_response(char* response, size_t response_size, int received)
{
	if (response == NULL || received < 0 || (size_t)received >= response_size) {
		return IRECV_E_USB_STATUS;
	}

	response[received] = '\0';
	return IRECV_E_SUCCESS;
}

#endif
