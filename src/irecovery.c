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

enum {
	kResetDevice, kStartShell, kSendCommand, kSendFile
};

void print_shell_usage() {
	printf("Usage:\n");
	printf("\t:f <file>\tSend file to device.\n");
	printf("\t:h\t\tShow this help.\n");
	printf("\t:q\t\tQuit interactive shell.\n");
}

void init_shell(irecv_device* device) {
	int ret;

	for(;;) {
		char* cmd = readline("iRecovery> ");
		if(cmd && *cmd) {
			add_history(cmd);
			if(cmd[0] == ':') {
				strtok(cmd, " ");
				char* arg = strtok(0, " ");
				
				if(cmd[1] == 'q') {
					free(cmd);
					break;
				} else if(cmd[1] == 'h') {
					print_shell_usage();
				} else if(cmd[1] == 'f') {
					ret = irecv_send_file(device, arg);
					// TODO: error messages
				}
			} else {
				ret = irecv_send_command(device, cmd);
				// TODO: error messages
			}

			free(cmd);
		}
	}
}

void print_usage() {
	printf("iRecovery - iDevice Recovery Utility\n");
	printf("Usage: ./irecovery [args]\n");
	printf("\t-c <cmd>\tSend command to device.\n");
	printf("\t-d\t\tStart irecovery in debug mode.\n");
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
	if(argc == 1) print_usage();
	while ((opt = getopt(argc, argv, "dhrsc:f:")) > 0) {
		switch (opt) {
		case 'd':
			irecv_set_debug(1);
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

		case 'c':
			action = kSendCommand;
			argument = optarg;
			break;

		case 'f':
			action = kSendFile;
			argument = optarg;
			break;

		default:
			fprintf(stderr, "Unknown argument\n");
			break;
		}
	}

	irecv_device* device = NULL;
	if(irecv_init(&device) < 0) {
		fprintf(stderr, "Unable to initialize libirecovery\n");
		return -1;
	}

	if(irecv_open(device) < 0) {
		fprintf(stderr, "Unable to open device\n");
		return -1;
	}

	switch(action) {
	case kResetDevice:
		irecv_reset(device);
		break;

	case kSendFile:
		irecv_send_file(device, argument);
		break;

	case kSendCommand:
		irecv_send_command(device, argument);
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

