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
#include <unistd.h>
#include <libirecovery.h>
#include <readline/readline.h>
#include <readline/history.h>

#define FILE_HISTORY_PATH ".irecovery"
#define debug(...) if(verbose) fprintf(stderr, __VA_ARGS__)

enum {
	kResetDevice, kStartShell, kSendCommand, kSendFile, kSendExploit
};

static unsigned int quit = 0;
static unsigned int verbose = 0;

int received_cb(irecv_client_t client, const irecv_event_t* event);
int precommand_cb(irecv_client_t client, const irecv_event_t* event);
int postcommand_cb(irecv_client_t client, const irecv_event_t* event);

void shell_usage() {
	printf("Usage:\n");
	printf("\t/upload <file>\tSend file to client.\n");
	printf("\t/exploit [file]\tSend usb exploit with optional payload\n");
	printf("\t/help\t\tShow this help.\n");
	printf("\t/exit\t\tExit interactive shell.\n");
}

void parse_command(irecv_client_t client, unsigned char* command, unsigned int size) {
	char* cmd = strdup(command);
	char* action = strtok(cmd, " ");
	debug("Executing %s\n", action);
	if (!strcmp(cmd, "/exit")) {
		quit = 1;
	} else

	if (!strcmp(cmd, "/help")) {
		shell_usage();
	} else

	if (!strcmp(cmd, "/upload")) {
		char* filename = strtok(NULL, " ");
		debug("Sending %s\n", filename);
		if (filename != NULL) {
			irecv_send_file(client, filename);
		}
	} else

	if (!strcmp(cmd, "/exploit")) {
		char* filename = strtok(NULL, " ");
		debug("Sending %s\n", filename);
		if (filename != NULL) {
			irecv_send_file(client, filename);
		}
		irecv_send_exploit(client);
	}

	free(action);
}

void load_command_history() {
	read_history(FILE_HISTORY_PATH);
}

void append_command_to_history(char* cmd) {
	add_history(cmd);
	write_history(FILE_HISTORY_PATH);
}

void init_shell(irecv_client_t client) {
	irecv_error_t error = 0;
	load_command_history();
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
			error = irecv_send_command(client, cmd);
			if (error != IRECV_E_SUCCESS) {
				quit = 1;
			}

			append_command_to_history(cmd);
			free(cmd);
		}
	}
}

int received_cb(irecv_client_t client, const irecv_event_t* event) {
	if (event->type == IRECV_RECEIVED) {
		int i = 0;
		int size = event->size;
		char* data = event->data;
		for (i = 0; i < size; i++) {
			printf("%c", data[i]);
		}
	}
	return 0;
}

int precommand_cb(irecv_client_t client, const irecv_event_t* event) {
	if (event->type == IRECV_PRECOMMAND) {
		irecv_error_t error = 0;
		if (event->data[0] == '/') {
			parse_command(client, event->data, event->size);
			return -1;
		}
	}
	return 0;
}

int postcommand_cb(irecv_client_t client, const irecv_event_t* event) {
	unsigned char* value = NULL;
	if (event->type == IRECV_POSTCOMMAND) {
		irecv_error_t error = 0;
		if (strstr(event->data, "getenv") != NULL) {
			error = irecv_getenv(client, &value);
			if (error != IRECV_E_SUCCESS) {
				debug("%s\n", irecv_strerror(error));
				return error;
			}
			printf("%s\n", value);
		}

		if (!strcmp(event->data, "reboot")) {
			quit = 1;
		}
	}

	if (value != NULL) free(value);
	return 0;
}

void print_usage() {
	printf("iRecovery - iDevice Recovery Utility\n");
	printf("Usage: ./irecovery [args]\n");
	printf("\t-v\t\tStart irecovery in verbose mode.\n");
	printf("\t-c <cmd>\tSend command to client.\n");
	printf("\t-f <file>\tSend file to client.\n");
	printf("\t-k [payload]\tSend usb exploit to client.\n");
	printf("\t-h\t\tShow this help.\n");
	printf("\t-r\t\tReset client.\n");
	printf("\t-s\t\tStart interactive shell.\n");
	exit(1);
}

int main(int argc, char** argv) {
	int i = 0;
	int opt = 0;
	int action = 0;
	char* argument = NULL;
	irecv_error_t error = 0;
	if (argc == 1) print_usage();
	while ((opt = getopt(argc, argv, "vhrsc:f:k::")) > 0) {
		switch (opt) {
		case 'v':
			verbose += 1;
			break;

		case 'h':
			print_usage();
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

		default:
			fprintf(stderr, "Unknown argument\n");
			return -1;
		}
	}

	irecv_client_t client = NULL;
	for (i = 0; i <= 5; i++) {
		debug("Attempting to connect... \n");

		if (irecv_open(&client) != IRECV_E_SUCCESS)
			sleep(1);
		else
			break;

		if (i == 5) {
			return -1;
		}
	}

	if (verbose) irecv_set_debug(client, verbose);

	switch (action) {
	case kResetDevice:
		irecv_reset(client);
		break;

	case kSendFile:
		error = irecv_send_file(client, argument);
		debug("%s\n", irecv_strerror(error));
		break;

	case kSendCommand:
		error = irecv_send_command(client, argument);
		debug("%s\n", irecv_strerror(error));
		break;

	case kSendExploit:
		if (argument != NULL) {
			error = irecv_send_file(client, argument);
			if (error != IRECV_E_SUCCESS) {
				debug("%s\n", irecv_strerror(error));
				break;
			}
		}
		error = irecv_send_exploit(client);
		debug("%s\n", irecv_strerror(error));
		break;

	case kStartShell:
		init_shell(client);
		break;

	default:
		fprintf(stderr, "Unknown action\n");
		break;
	}

	irecv_close(client);
	return 0;
}

