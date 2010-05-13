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
#include <libirecovery.h>

int main(int argc, char** argv) {
	irecv_device* device = NULL;
	if(irecv_init(&device) < 0) {
		fprintf(stderr, "Unable to initialize libirecovery\n");
		return -1;
	}

	if(irecv_open(device) < 0) {
		fprintf(stderr, "Unable to open device\n");
		return -1;
	}

	switch (device->mode) {
	case kRecoveryMode:
		printf("Found device in recovery mode\n");
		break;

	case kDfuMode:
		printf("Found device in DFU mode\n");
		break;

	case kKernelMode:
		printf("Found device in kernel mode\n");
		break;

	default:
		printf("No device found\n");
		break;
	}

	irecv_exit(device);
	return 0;
}

