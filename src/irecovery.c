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

enum {
	kResetDevice, kSendCommand, kSendFile
};

void print_usage() {
	printf("iRecovery - iDevice Recovery Utility\n");
	printf("Usage: ./irecovery [args]\n");
	exit(1);
}

int main(int argc, char** argv) {
	int opt = 0;
	int action = 0;
	char* argument = NULL;
	if(argc == 1) print_usage();
	while ((opt = getopt(argc, argv, "dhrc:f:")) > 0) {
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

	default:
		fprintf(stderr, "Unknown action\n");
		break;
	}

	irecv_exit(device);
	return 0;
}

