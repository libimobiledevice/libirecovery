#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "libirecovery-private.h"

#define TEST_PIPE (-101)
#define TEST_TIMEOUT (-102)
#define TEST_NO_DEVICE (-103)
#define TEST_NO_MEMORY (-104)

static int test_halt_handling(void)
{
	return !irecv_usb_should_clear_halt(TEST_TIMEOUT, TEST_PIPE)
		&& irecv_usb_should_clear_halt(TEST_PIPE, TEST_PIPE);
}

static int test_command_policies(void)
{
	static const struct {
		const char* command;
		int expected_b_request;
		int expected_teardown;
	} cases[] = {
		{ "go", 1, 1 },
		{ "go 0x1", 0, 1 },
		{ "bootx", 1, 1 },
		{ "bootx kernel", 0, 1 },
		{ "reboot", 1, 1 },
		{ "reboot normal", 0, 1 },
		{ "memboot", 1, 1 },
		{ "memboot 0x800000000", 0, 1 },
		{ "\tgo\t0x1", 0, 1 },
		{ "go ", 0, 1 },
		{ " bootx", 0, 1 },
		{ "reboot\n", 0, 1 },
		{ "getenv auto-boot", 0, 0 },
		{ "setenv go 1", 0, 0 },
		{ "goober", 0, 0 },
		{ "bootx-extra", 0, 0 },
		{ "reboot_now", 0, 0 },
		{ "memboot2", 0, 0 },
		{ "", 0, 0 },
		{ " \t", 0, 0 },
		{ NULL, 0, 0 }
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (irecv_command_uses_b_request(cases[i].command) != cases[i].expected_b_request
			|| irecv_command_is_device_teardown(cases[i].command) != cases[i].expected_teardown) {
			return 0;
		}
	}
	return 1;
}

static int test_command_error_acceptance(void)
{
	static const char* const teardown_commands[] = {
		"go",
		"go 0x1",
		"bootx",
		"bootx kernel",
		"reboot",
		"reboot normal",
		"memboot",
		"memboot 0x800000000",
		"\tgo\t0x1",
		"go ",
		" bootx",
		"reboot\n"
	};
	static const char* const regular_commands[] = {
		"getenv auto-boot",
		"setenv auto-boot true",
		"goober",
		"memboot2 argument"
	};
	static const irecv_error_t teardown_errors[] = {
		IRECV_E_PIPE,
		IRECV_E_NO_DEVICE,
		IRECV_E_TIMEOUT,
		IRECV_E_USB_STATUS
	};
	static const irecv_error_t rejected_errors[] = {
		IRECV_E_OUT_OF_MEMORY,
		IRECV_E_INVALID_INPUT,
		IRECV_E_UNABLE_TO_CONNECT,
		IRECV_E_SUCCESS
	};
	size_t command_index;
	size_t error_index;

	for (command_index = 0; command_index < sizeof(teardown_commands) / sizeof(teardown_commands[0]); command_index++) {
		for (error_index = 0; error_index < sizeof(teardown_errors) / sizeof(teardown_errors[0]); error_index++) {
			if (!irecv_command_error_is_accepted(teardown_errors[error_index], teardown_commands[command_index])) {
				return 0;
			}
		}
		for (error_index = 0; error_index < sizeof(rejected_errors) / sizeof(rejected_errors[0]); error_index++) {
			if (irecv_command_error_is_accepted(rejected_errors[error_index], teardown_commands[command_index])) {
				return 0;
			}
		}
	}

	for (command_index = 0; command_index < sizeof(regular_commands) / sizeof(regular_commands[0]); command_index++) {
		if (!irecv_command_error_is_accepted(IRECV_E_PIPE, regular_commands[command_index])) {
			return 0;
		}
		for (error_index = 1; error_index < sizeof(teardown_errors) / sizeof(teardown_errors[0]); error_index++) {
			if (irecv_command_error_is_accepted(teardown_errors[error_index], regular_commands[command_index])) {
				return 0;
			}
		}
	}

	return 1;
}

static int test_error_mapping(void)
{
	return irecv_map_control_transfer_error(TEST_PIPE, TEST_PIPE, TEST_TIMEOUT, TEST_NO_DEVICE, TEST_NO_MEMORY) == IRECV_E_PIPE
		&& irecv_map_control_transfer_error(TEST_TIMEOUT, TEST_PIPE, TEST_TIMEOUT, TEST_NO_DEVICE, TEST_NO_MEMORY) == IRECV_E_TIMEOUT
		&& irecv_map_control_transfer_error(TEST_NO_DEVICE, TEST_PIPE, TEST_TIMEOUT, TEST_NO_DEVICE, TEST_NO_MEMORY) == IRECV_E_NO_DEVICE
		&& irecv_map_control_transfer_error(TEST_NO_MEMORY, TEST_PIPE, TEST_TIMEOUT, TEST_NO_DEVICE, TEST_NO_MEMORY) == IRECV_E_OUT_OF_MEMORY
		&& irecv_map_control_transfer_error(-105, TEST_PIPE, TEST_TIMEOUT, TEST_NO_DEVICE, TEST_NO_MEMORY) == IRECV_E_USB_STATUS
		&& irecv_preserve_control_transfer_error(IRECV_E_UNKNOWN_ERROR) == IRECV_E_UNKNOWN_ERROR
		&& irecv_preserve_control_transfer_error(-123) == IRECV_E_USB_STATUS;
}

static int test_response_finalization(void)
{
	char empty[1] = {'x'};
	char response[4] = {'a', 'b', 'c', 'x'};
	char bounded[4] = {'a', 'b', 'c', 'x'};

	return irecv_finalize_control_response(empty, sizeof(empty), 0) == IRECV_E_SUCCESS
		&& empty[0] == '\0'
		&& irecv_finalize_control_response(response, sizeof(response), 3) == IRECV_E_SUCCESS
		&& strcmp(response, "abc") == 0
		&& irecv_finalize_control_response(bounded, sizeof(bounded), 4) == IRECV_E_USB_STATUS
		&& bounded[3] == 'x'
		&& irecv_finalize_control_response(NULL, 0, 0) == IRECV_E_USB_STATUS;
}

int main(void)
{
	if (!test_halt_handling()) {
		return 1;
	}
	if (!test_command_policies()) {
		return 2;
	}
	if (!test_command_error_acceptance()) {
		return 3;
	}
	if (!test_error_mapping()) {
		return 4;
	}
	if (!test_response_finalization()) {
		return 5;
	}
	return 0;
}
