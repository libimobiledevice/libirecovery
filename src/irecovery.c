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
	kResetDevice, kStartShell, kSendCommand, kSendFile
};

static unsigned int quit = 0;
static unsigned int verbose = 0;

void print_shell_usage() {
	printf("Usage:\n");
	printf("\t/upload <file>\tSend file to device.\n");
	printf("\t/help\t\tShow this help.\n");
	printf("\t/exit\t\tExit interactive shell.\n");
}

void parse_command(irecv_device_t* device, unsigned char* command, unsigned int size) {
	char* cmd = strtok(strdup(command), " ");
	debug("Executing %s %s\n", cmd, command);
	if(!strcmp(cmd, "/exit")) {
		quit = 1;
	} else
	
	if(!strcmp(cmd, "/help")) {
		print_shell_usage();
	} else

	if(!strcmp(cmd, "/reconnect")) {
		irecv_close(device);
		irecv_open(device);
	} else
	
	if(!strcmp(cmd, "/upload")) {
		char* filename = strtok(NULL, " ");
		debug("Sending %s\n", filename);
		if(filename != NULL) {
			irecv_send_file(device, filename);
		}
	}
	free(cmd);
}

int recv_callback(irecv_device_t* device, unsigned char* data, int size) {
	int i = 0;
	for(i = 0; i < size; i++) {
		printf("%c", data[i]);
	}
	return size;
}

int send_callback(irecv_device_t* device, unsigned char* command, int size) {
	irecv_error_t error = 0;
	if(command[0] == '/') {
		parse_command(device, command, size);
		return 0;
	}

	if(strstr(command, "getenv") != NULL) {
		unsigned char* value = NULL;
		error = irecv_send_command(device, command);
		if(error != IRECV_SUCCESS) {
			debug("%s\n", irecv_strerror(error));
			return error;
		}

		error = irecv_getenv(device, &value);
		if(error != IRECV_SUCCESS) {
			debug("%s\n", irecv_strerror(error));
			return error;
		}

		printf("%s\n", value);
		free(value);
		return 0;
	}

	if(!strcmp(command, "reboot")) {
		error = irecv_send_command(device, command);
		if(error != IRECV_SUCCESS) {
			debug("%s\n", irecv_strerror(error));
			return error;
		}
		quit = 1;
		return 0;
	}

	return size;
}

void load_command_history() {
	read_history(FILE_HISTORY_PATH);
}

void append_command_to_history(char* cmd) {
	add_history(cmd);
	write_history(FILE_HISTORY_PATH);
}

void init_shell(irecv_device_t* device) {
	irecv_error_t error = 0;
	load_command_history();
	irecv_set_sender(device, &send_callback);
	irecv_set_receiver(device, &recv_callback);
	while(!quit) {
		error = irecv_receive(device);
		if(error != IRECV_SUCCESS) {
			debug("%s\n", irecv_strerror(error));
			break;
		}
		
		char* cmd = readline("> ");
		if(cmd && *cmd) {
			error = irecv_send(device, cmd);
			if(error != IRECV_SUCCESS) {
				quit = 1;
			}
			
			append_command_to_history(cmd);
			free(cmd);
		}
	}
}

void print_usage() {
	printf("iRecovery - iDevice Recovery Utility\n");
	printf("Usage: ./irecovery [args]\n");
	printf("\t-v\t\tStart irecovery in verbose mode.\n");
	printf("\t-c <cmd>\tSend command to device.\n");
	printf("\t-f <file>\tSend file to device.\n");
	printf("\t-h\t\tShow this help.\n");
	printf("\t-r\t\tReset device.\n");
	printf("\t-s\t\tStart interactive shell.\n");
	exit(1);
}

int main(int argc, char** argv) {
	int opt = 0;
	int action = 0;
	char* argument = NULL;
	irecv_error_t error = 0;
	if(argc == 1) print_usage();
	while ((opt = getopt(argc, argv, "vhrsc:f:")) > 0) {
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

		default:
			fprintf(stderr, "Unknown argument\n");
			break;
		}
	}

	irecv_device_t* device = irecv_init();
	if(device == NULL) {
		fprintf(stderr, "Unable to initialize libirecovery\n");
		return -1;
	}
	if(verbose) irecv_set_debug(device, verbose);

	int i = 0;
	for(i = 0; i <= 5; i++) {
		debug("Attempting to connect... ");
		if(i == 5) {
			irecv_exit(device);
			return -1;
		}

		if(irecv_open(device) < 0) sleep(1);
		else break;
		debug("failed\n");
	}

	switch(action) {
	case kResetDevice:
		irecv_reset(device);
		break;

	case kSendFile:
		error = irecv_send_file(device, argument);
		debug("%s\n", irecv_strerror(error));
		break;

	case kSendCommand:
		error = irecv_send_command(device, argument);
		debug("%s\n", irecv_strerror(error));
		break;

	case kStartShell:
		init_shell(device);
		break;

	default:
		fprintf(stderr, "Unknown action\n");
		break;
	}

	irecv_exit(device);
	return 0;
}

