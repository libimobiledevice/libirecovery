/*
 * irecovery.c
 * Software frontend for iBoot/iBSS communication with iOS devices
 *
 * Copyright (c) 2012-2023 Nikias Bassen <nikias@gmx.li>
 * Copyright (c) 2012-2015 Martin Szulecki <martin.szulecki@libimobiledevice.org>
 * Copyright (c) 2010-2011 Chronic-Dev Team
 * Copyright (c) 2010-2011 Joshua Hill
 * Copyright (c) 2008-2011 Nicolas Haunold
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define TOOL_NAME "irecovery"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <libirecovery.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef WIN32
#include <windows.h>
#ifndef sleep
#define sleep(n) Sleep(1000 * n)
#endif
#endif

#define FILE_HISTORY_PATH ".irecovery"
#define debug(...) if (verbose) fprintf(stderr, __VA_ARGS__)

enum {
	kNoAction,
	kResetDevice,
	kStartShell,
	kSendCommand,
	kSendFile,
	kSendExploit,
	kSendScript,
	kShowMode,
	kRebootToNormalMode,
	kQueryInfo,
	kListDevices
};

static unsigned int quit = 0;
static unsigned int verbose = 0;

void print_progress_bar(double progress);
int received_cb(irecv_client_t client, const irecv_event_t* event);
int progress_cb(irecv_client_t client, const irecv_event_t* event);
int precommand_cb(irecv_client_t client, const irecv_event_t* event);
int postcommand_cb(irecv_client_t client, const irecv_event_t* event);

static void shell_usage()
{
	printf("Usage:\n");
	printf("  /upload FILE\t\tsend FILE to device\n");
	printf("  /limera1n [FILE]\trun limera1n exploit and send optional payload from FILE\n");
	printf("  /deviceinfo\t\tprint device information (ECID, IMEI, etc.)\n");
	printf("  /help\t\t\tshow this help\n");
	printf("  /exit\t\t\texit interactive shell\n");
}

static const char* mode_to_str(int mode)
{
	switch (mode) {
		case IRECV_K_RECOVERY_MODE_1:
		case IRECV_K_RECOVERY_MODE_2:
		case IRECV_K_RECOVERY_MODE_3:
		case IRECV_K_RECOVERY_MODE_4:
			return "Recovery";
			break;
		case IRECV_K_DFU_MODE:
			return "DFU";
			break;
		case IRECV_K_PORT_DFU_MODE:
			return "Port DFU";
			break;
		case IRECV_K_WTF_MODE:
			return "WTF";
			break;
		default:
			return "Unknown";
			break;
	}
}

static void buffer_read_from_filename(const char *filename, char **buffer, uint64_t *length)
{
	FILE *f;
	uint64_t size;

	*length = 0;

	f = fopen(filename, "rb");
	if (!f) {
		return;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);

	if (size == 0) {
		fclose(f);
		return;
	}

	*buffer = (char*)malloc(sizeof(char)*(size+1));
	fread(*buffer, sizeof(char), size, f);
	fclose(f);

	*length = size;
}

static void print_hex(unsigned char *buf, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++) {
		printf("%02x", buf[i]);
	}
}

static void print_device_info(irecv_client_t client)
{
	int ret, mode;
	irecv_device_t device = NULL;
	const struct irecv_device_info *devinfo = irecv_get_device_info(client);
	if (devinfo) {
		printf("CPID: 0x%04x\n", devinfo->cpid);
		printf("CPRV: 0x%02x\n", devinfo->cprv);
		printf("BDID: 0x%02x\n", devinfo->bdid);
		printf("ECID: 0x%016" PRIx64 "\n", devinfo->ecid);
		printf("CPFM: 0x%02x\n", devinfo->cpfm);
		printf("SCEP: 0x%02x\n", devinfo->scep);
		printf("IBFL: 0x%02x\n", devinfo->ibfl);
		printf("SRTG: %s\n", (devinfo->srtg) ? devinfo->srtg : "N/A");
		printf("SRNM: %s\n", (devinfo->srnm) ? devinfo->srnm : "N/A");
		printf("IMEI: %s\n", (devinfo->imei) ? devinfo->imei : "N/A");
		printf("NONC: ");
		if (devinfo->ap_nonce) {
			print_hex(devinfo->ap_nonce, devinfo->ap_nonce_size);
		} else {
			printf("N/A");
		}
		printf("\n");
		printf("SNON: ");
		if (devinfo->sep_nonce) {
			print_hex(devinfo->sep_nonce, devinfo->sep_nonce_size);
		} else {
			printf("N/A");
		}
		printf("\n");
		char* p = strstr(devinfo->serial_string, "PWND:[");
		if (p) {
			p+=6;
			char* pend = strchr(p, ']');
			if (pend) {
				printf("PWND: %.*s\n", (int)(pend-p), p);
			}
		}
	} else {
		printf("Could not get device info?!\n");
	}

	ret = irecv_get_mode(client, &mode);
	if (ret == IRECV_E_SUCCESS) {
		switch (devinfo->pid) {
			case 0x1881:
				printf("MODE: DFU via Debug USB (KIS)\n");
				break;
			default:
				printf("MODE: %s\n", mode_to_str(mode));
				break;
		}
	}

	irecv_devices_get_device_by_client(client, &device);
	if (device) {
		printf("PRODUCT: %s\n", device->product_type);
		printf("MODEL: %s\n", device->hardware_model);
		printf("NAME: %s\n", device->display_name);
	}
}

static void print_devices()
{
	struct irecv_device *devices = irecv_devices_get_all();
	struct irecv_device *device = NULL;
	int i = 0;

	for (i = 0; devices[i].product_type != NULL; i++) {
		device = &devices[i];

		printf("%s %s 0x%02x 0x%04x %s\n", device->product_type, device->hardware_model, device->board_id, device->chip_id, device->display_name);
	}
}

static int _is_breq_command(const char* cmd)
{
	return (
		!strcmp(cmd, "go")
		|| !strcmp(cmd, "bootx")
		|| !strcmp(cmd, "reboot")
		|| !strcmp(cmd, "memboot")
	);
}

static void parse_command(irecv_client_t client, unsigned char* command, unsigned int size)
{
	char* cmd = strdup((char*)command);
	char* action = strtok(cmd, " ");

	if (!strcmp(cmd, "/exit")) {
		quit = 1;
	} else if (!strcmp(cmd, "/help")) {
		shell_usage();
	} else if (!strcmp(cmd, "/upload")) {
		char* filename = strtok(NULL, " ");
		debug("Uploading file %s\n", filename);
		if (filename != NULL) {
			irecv_send_file(client, filename, 0);
		}
	} else if (!strcmp(cmd, "/deviceinfo")) {
		print_device_info(client);
	} else if (!strcmp(cmd, "/limera1n")) {
		char* filename = strtok(NULL, " ");
		debug("Sending limera1n payload %s\n", filename);
		if (filename != NULL) {
			irecv_send_file(client, filename, 0);
		}
		irecv_trigger_limera1n_exploit(client);
	} else if (!strcmp(cmd, "/execute")) {
		char* filename = strtok(NULL, " ");
		debug("Executing script %s\n", filename);
		if (filename != NULL) {
			char* buffer = NULL;
			uint64_t buffer_length = 0;
			buffer_read_from_filename(filename, &buffer, &buffer_length);
			if (buffer) {
				buffer[buffer_length] = '\0';
				irecv_execute_script(client, buffer);
				free(buffer);
			} else {
				printf("Could not read file '%s'\n", filename);
			}
		}
	} else {
		printf("Unsupported command %s. Use /help to get a list of available commands.\n", cmd);
	}

	free(action);
}

static void load_command_history()
{
	read_history(FILE_HISTORY_PATH);
}

static void append_command_to_history(char* cmd)
{
	add_history(cmd);
	write_history(FILE_HISTORY_PATH);
}

static void init_shell(irecv_client_t client)
{
	irecv_error_t error = 0;
	load_command_history();
	irecv_event_subscribe(client, IRECV_PROGRESS, &progress_cb, NULL);
	irecv_event_subscribe(client, IRECV_RECEIVED, &received_cb, NULL);
	irecv_event_subscribe(client, IRECV_PRECOMMAND, &precommand_cb, NULL);
	irecv_event_subscribe(client, IRECV_POSTCOMMAND, &postcommand_cb, NULL);
	while (!quit) {
		error = irecv_receive(client);
		if (error != IRECV_E_SUCCESS) {
			debug("%s\n", irecv_strerror(error));
			break;
		}

		char* cmd = readline("> ");
		if (cmd && *cmd) {
			if (_is_breq_command(cmd)) {
				error = irecv_send_command_breq(client, cmd, 1);
			} else {
				error = irecv_send_command(client, cmd);
			}
			if (error != IRECV_E_SUCCESS) {
				quit = 1;
			}

			append_command_to_history(cmd);
			free(cmd);
		}
	}
}

int received_cb(irecv_client_t client, const irecv_event_t* event)
{
	if (event->type == IRECV_RECEIVED) {
		int i = 0;
		int size = event->size;
		const char* data = event->data;
		for (i = 0; i < size; i++) {
			printf("%c", data[i]);
		}
	}

	return 0;
}

int precommand_cb(irecv_client_t client, const irecv_event_t* event)
{
	if (event->type == IRECV_PRECOMMAND) {
		if (event->data[0] == '/') {
			parse_command(client, (unsigned char*)event->data, event->size);
			return -1;
		}
	}

	return 0;
}

int postcommand_cb(irecv_client_t client, const irecv_event_t* event)
{
	char* value = NULL;
	char* action = NULL;
	char* command = NULL;
	char* argument = NULL;
	irecv_error_t error = IRECV_E_SUCCESS;

	if (event->type == IRECV_POSTCOMMAND) {
		command = strdup(event->data);
		action = strtok(command, " ");
		if (!strcmp(action, "getenv")) {
			argument = strtok(NULL, " ");
			error = irecv_getenv(client, argument, &value);
			if (error != IRECV_E_SUCCESS) {
				debug("%s\n", irecv_strerror(error));
				free(command);
				return error;
			}
			printf("%s\n", value);
			free(value);
		}

		if (!strcmp(action, "reboot")) {
			quit = 1;
		}
	}

	free(command);

	return 0;
}

int progress_cb(irecv_client_t client, const irecv_event_t* event)
{
	if (event->type == IRECV_PROGRESS) {
		print_progress_bar(event->progress);
	}

	return 0;
}

void print_progress_bar(double progress)
{
	int i = 0;

	if (progress < 0) {
		return;
	}

	if (progress > 100) {
		progress = 100;
	}

	printf("\r[");

	for (i = 0; i < 50; i++) {
		if (i < progress / 2) {
			printf("=");
		} else {
			printf(" ");
		}
	}

	printf("] %3.1f%%", progress);

	fflush(stdout);

	if (progress == 100) {
		printf("\n");
	}
}

static void print_usage(int argc, char **argv)
{
	char *name = NULL;
	name = strrchr(argv[0], '/');
	printf("Usage: %s [OPTIONS]\n", (name ? name + 1: argv[0]));
	printf("\n");
	printf("Interact with an iOS device in DFU or recovery mode.\n");
	printf("\n");
	printf("OPTIONS:\n");
	printf("  -i, --ecid ECID\tconnect to specific device by its ECID\n");
	printf("  -c, --command CMD\trun CMD on device\n");
	printf("  -m, --mode\t\tprint current device mode\n");
	printf("  -f, --file FILE\tsend file to device\n");
	printf("  -k, --payload FILE\tsend limera1n usb exploit payload from FILE\n");
	printf("  -r, --reset\t\treset client\n");
	printf("  -n, --normal\t\treboot device into normal mode (exit recovery loop)\n");
	printf("  -e, --script FILE\texecutes recovery script from FILE\n");
	printf("  -s, --shell\t\tstart an interactive shell\n");
	printf("  -q, --query\t\tquery device info\n");
	printf("  -a, --devices\t\tlist information for all known devices\n");
	printf("  -v, --verbose\t\tenable verbose output, repeat for higher verbosity\n");
	printf("  -h, --help\t\tprints this usage information\n");
	printf("  -V, --version\t\tprints version information\n");
	printf("\n");
	printf("Homepage:    <" PACKAGE_URL ">\n");
	printf("Bug Reports: <" PACKAGE_BUGREPORT ">\n");
}

int main(int argc, char* argv[])
{
	static struct option longopts[] = {
		{ "ecid",    required_argument, NULL, 'i' },
		{ "command", required_argument, NULL, 'c' },
		{ "mode",    no_argument,       NULL, 'm' },
		{ "file",    required_argument, NULL, 'f' },
		{ "payload", required_argument, NULL, 'k' },
		{ "reset",   no_argument,       NULL, 'r' },
		{ "normal",  no_argument,       NULL, 'n' },
		{ "script",  required_argument, NULL, 'e' },
		{ "shell",   no_argument,       NULL, 's' },
		{ "query",   no_argument,       NULL, 'q' },
		{ "devices", no_argument,       NULL, 'a' },
		{ "verbose", no_argument,       NULL, 'v' },
		{ "help",    no_argument,       NULL, 'h' },
		{ "version", no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};
	int i = 0;
	int opt = 0;
	int action = kNoAction;
	uint64_t ecid = 0;
	int mode = -1;
	char* argument = NULL;
	irecv_error_t error = 0;

	char* buffer = NULL;
	uint64_t buffer_length = 0;

	if (argc == 1) {
		print_usage(argc, argv);
		return 0;
	}

	while ((opt = getopt_long(argc, argv, "i:vVhrsmnc:f:e:k:qa", longopts, NULL)) > 0) {
		switch (opt) {
			case 'i':
				if (optarg) {
					char* tail = NULL;
					ecid = strtoull(optarg, &tail, 0);
					if (tail && (tail[0] != '\0')) {
						ecid = 0;
					}
					if (ecid == 0) {
						fprintf(stderr, "ERROR: Could not parse ECID from argument '%s'\n", optarg);
						return -1;
					}
				}
				break;

			case 'v':
				verbose += 1;
				break;

			case 'h':
				print_usage(argc, argv);
				return 0;

			case 'm':
				action = kShowMode;
				break;

			case 'n':
				action = kRebootToNormalMode;
				break;

			case 'r':
				action = kResetDevice;
				break;

			case 's':
				action = kStartShell;
				break;

			case 'f':
				action = kSendFile;
				argument = optarg;
				break;

			case 'c':
				action = kSendCommand;
				argument = optarg;
				break;

			case 'k':
				action = kSendExploit;
				argument = optarg;
				break;

			case 'e':
				action = kSendScript;
				argument = optarg;
				break;

			case 'q':
				action = kQueryInfo;
				break;

			case 'a':
				action = kListDevices;
				print_devices();
				return 0;

			case 'V':
				printf("%s %s\n", TOOL_NAME, PACKAGE_VERSION);
				return 0;

			default:
				fprintf(stderr, "Unknown argument\n");
				return -1;
		}
	}

	if (action == kNoAction) {
		fprintf(stderr, "ERROR: Missing action option\n");
		print_usage(argc, argv);
		return -1;
	}

	if (verbose)
		irecv_set_debug_level(verbose);

	irecv_client_t client = NULL;
	for (i = 0; i <= 5; i++) {
		debug("Attempting to connect... \n");

		irecv_error_t err = irecv_open_with_ecid(&client, ecid);
		if (err == IRECV_E_UNSUPPORTED) {
			fprintf(stderr, "ERROR: %s\n", irecv_strerror(err));
			return -1;
		}
		else if (err != IRECV_E_SUCCESS)
			sleep(1);
		else
			break;

		if (i == 5) {
			fprintf(stderr, "ERROR: %s\n", irecv_strerror(err));
			return -1;
		}
	}

	irecv_device_t device = NULL;
	irecv_devices_get_device_by_client(client, &device);
	if (device)
		debug("Connected to %s, model %s, cpid 0x%04x, bdid 0x%02x\n", device->product_type, device->hardware_model, device->chip_id, device->board_id);

	const struct irecv_device_info *devinfo = irecv_get_device_info(client);

	switch (action) {
		case kResetDevice:
			irecv_reset(client);
			break;

		case kSendFile:
			irecv_event_subscribe(client, IRECV_PROGRESS, &progress_cb, NULL);
			error = irecv_send_file(client, argument, IRECV_SEND_OPT_DFU_NOTIFY_FINISH);
			debug("%s\n", irecv_strerror(error));
			break;

		case kSendCommand:
			if (devinfo->pid == 0x1881) {
				printf("Shell is not available in Debug USB (KIS) mode.\n");
				break;
			}
			if (_is_breq_command(argument)) {
				error = irecv_send_command_breq(client, argument, 1);
			} else {
				error = irecv_send_command(client, argument);
			}
			debug("%s\n", irecv_strerror(error));
			break;

		case kSendExploit:
			if (devinfo->pid == 0x1881) {
				printf("Shell is not available in Debug USB (KIS) mode.\n");
				break;
			}
			if (argument != NULL) {
				irecv_event_subscribe(client, IRECV_PROGRESS, &progress_cb, NULL);
				error = irecv_send_file(client, argument, 0);
				if (error != IRECV_E_SUCCESS) {
					debug("%s\n", irecv_strerror(error));
					break;
				}
			}
			error = irecv_trigger_limera1n_exploit(client);
			debug("%s\n", irecv_strerror(error));
			break;

		case kStartShell:
			if (devinfo->pid == 0x1881) {
				printf("This feature is not supported in Debug USB (KIS) mode.\n");
				break;
			}
			init_shell(client);
			break;

		case kSendScript:
			if (devinfo->pid == 0x1881) {
				printf("This feature is not supported in Debug USB (KIS) mode.\n");
				break;
			}
			buffer_read_from_filename(argument, &buffer, &buffer_length);
			if (buffer) {
				buffer[buffer_length] = '\0';

				error = irecv_execute_script(client, buffer);
				if (error != IRECV_E_SUCCESS) {
					debug("%s\n", irecv_strerror(error));
				}

				free(buffer);
			} else {
				fprintf(stderr, "Could not read file '%s'\n", argument);
			}
			break;

		case kShowMode: {
			irecv_get_mode(client, &mode);
			printf("%s Mode", mode_to_str(mode));
			if (devinfo->pid == 0x1881) {
				printf(" via Debug USB (KIS)");
			}
			printf("\n");
			break;
		}
		case kRebootToNormalMode:
			if (devinfo->pid == 0x1881) {
				printf("This feature is not supported in Debug USB (KIS) mode.\n");
				break;
			}
			error = irecv_setenv(client, "auto-boot", "true");
			if (error != IRECV_E_SUCCESS) {
				debug("%s\n", irecv_strerror(error));
				break;
			}

			error = irecv_saveenv(client);
			if (error != IRECV_E_SUCCESS) {
				debug("%s\n", irecv_strerror(error));
				break;
			}

			error = irecv_reboot(client);
			if (error != IRECV_E_SUCCESS) {
				debug("%s\n", irecv_strerror(error));
			} else {
				debug("%s\n", irecv_strerror(error));
			}
			break;

		case kQueryInfo:
			print_device_info(client);
			break;

		default:
			fprintf(stderr, "Unknown action\n");
			break;
	}

	irecv_close(client);

	return 0;
}
