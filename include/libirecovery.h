/*
 * libirecovery.h - communication to iBoot/iBSS on Apple iOS devices via USB
 *
 * Copyright (c) 2012-2013 Martin Szulecki <m.szulecki@libimobiledevice.org>
 * Copyright (c) 2010 Chronic-Dev Team
 * Copyright (c) 2010 Joshua Hill
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

#ifndef LIBIRECOVERY_H
#define LIBIRECOVERY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifndef WIN32
#include <libusb-1.0/libusb.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef interface
#ifndef sleep
#define sleep(n) Sleep(1000 * n)
#endif
#endif

#define APPLE_VENDOR_ID 0x05AC
#define CPID_UNKNOWN -1
#define BDID_UNKNOWN -1

enum {
	kRecoveryMode1 = 0x1280,
	kRecoveryMode2 = 0x1281,
	kRecoveryMode3 = 0x1282,
	kRecoveryMode4 = 0x1283,
	kWTFMode = 0x1222,
	kDfuMode = 0x1227
};

typedef enum {
	IRECV_E_SUCCESS = 0,
	IRECV_E_NO_DEVICE = -1,
	IRECV_E_OUT_OF_MEMORY = -2,
	IRECV_E_UNABLE_TO_CONNECT = -3,
	IRECV_E_INVALID_INPUT = -4,
	IRECV_E_FILE_NOT_FOUND = -5,
	IRECV_E_USB_UPLOAD = -6,
	IRECV_E_USB_STATUS = -7,
	IRECV_E_USB_INTERFACE = -8,
	IRECV_E_USB_CONFIGURATION = -9,
	IRECV_E_PIPE = -10,
	IRECV_E_TIMEOUT = -11,
	IRECV_E_UNKNOWN_ERROR = -255
} irecv_error_t;

typedef enum {
	IRECV_RECEIVED = 1,
	IRECV_PRECOMMAND = 2,
	IRECV_POSTCOMMAND = 3,
	IRECV_CONNECTED = 4,
	IRECV_DISCONNECTED = 5,
	IRECV_PROGRESS = 6
} irecv_event_type;

typedef struct {
	int size;
	const char* data;
	double progress;
	irecv_event_type type;
} irecv_event_t;

struct irecv_client;
typedef struct irecv_client* irecv_client_t;
typedef struct irecv_device* irecv_device_t;
typedef int(*irecv_event_cb_t)(irecv_client_t client, const irecv_event_t* event);

struct irecv_client {
	int debug;
	int config;
	int interface;
	int alt_interface;
	unsigned short mode;
	char serial[256];
	
#ifndef WIN32
	libusb_device_handle* handle;
#else
	HANDLE handle;
	HANDLE hDFU;
	HANDLE hIB;
	LPSTR iBootPath;
	LPSTR DfuPath;
#endif
	
	irecv_event_cb_t progress_callback;
	irecv_event_cb_t received_callback;
	irecv_event_cb_t connected_callback;
	irecv_event_cb_t precommand_callback;
	irecv_event_cb_t postcommand_callback;
	irecv_event_cb_t disconnected_callback;
};

#define DEVICE_UNKNOWN      -1
#define DEVICE_IPHONE2G      0
#define DEVICE_IPHONE3G      1
#define DEVICE_IPHONE3GS     2
#define DEVICE_IPHONE4       3
#define DEVICE_IPHONE4REVA   4
#define DEVICE_IPHONE4CDMA   5
#define DEVICE_IPHONE4S      6
#define DEVICE_IPHONE51      7
#define DEVICE_IPHONE52      8
#define DEVICE_IPHONE53      9
#define DEVICE_IPHONE54     10
#define DEVICE_IPHONE61     11
#define DEVICE_IPHONE62     12
#define DEVICE_IPOD1G       13
#define DEVICE_IPOD2G       14
#define DEVICE_IPOD3G       15
#define DEVICE_IPOD4G       16
#define DEVICE_IPOD5G       17
#define DEVICE_IPAD1G       18
#define DEVICE_IPAD21       19
#define DEVICE_IPAD22       20
#define DEVICE_IPAD23       21
#define DEVICE_IPAD24       22
#define DEVICE_IPAD25       23
#define DEVICE_IPAD26       24
#define DEVICE_IPAD27       25
#define DEVICE_IPAD31       26
#define DEVICE_IPAD32       27
#define DEVICE_IPAD33       28
#define DEVICE_IPAD34       29
#define DEVICE_IPAD35       30
#define DEVICE_IPAD36       31
#define DEVICE_APPLETV2     32
#define DEVICE_APPLETV31    33
#define DEVICE_APPLETV32    34

struct irecv_device {
	int index;
	const char* product;
	const char* model;
	unsigned int board_id;
	unsigned int chip_id;
};

static struct irecv_device irecv_devices[] = {
	{  0, "iPhone1,1",  "m68ap", 0x00, 0x8900 },
	{  1, "iPhone1,2",  "n82ap", 0x04, 0x8900 },
	{  2, "iPhone2,1",  "n88ap", 0x00, 0x8920 },
	{  3, "iPhone3,1",  "n90ap", 0x00, 0x8930 },
	{  4, "iPhone3,2", "n90bap", 0x04, 0x8930 },
	{  5, "iPhone3,3",  "n92ap", 0x06, 0x8930 },
	{  6, "iPhone4,1",  "n94ap", 0x08, 0x8940 },
	{  7, "iPhone5,1",  "n41ap", 0x00, 0x8950 },
	{  8, "iPhone5,2",  "n42ap", 0x02, 0x8950 },
	{  9, "iPhone5,3",  "n48ap", 0x0a, 0x8950 },
	{ 10, "iPhone5,4",  "n49ap", 0x0e, 0x8950 },
	{ 11, "iPhone6,1",  "n51ap", 0x00, 0x8960 },
	{ 12, "iPhone6,2",  "n53ap", 0x02, 0x8960 },
	{ 13, "iPod1,1",    "n45ap", 0x02, 0x8900 },
	{ 14, "iPod2,1",    "n72ap", 0x00, 0x8720 },
	{ 15, "iPod3,1",    "n18ap", 0x02, 0x8922 },
	{ 16, "iPod4,1",    "n81ap", 0x08, 0x8930 },
	{ 17, "iPod5,1",    "n78ap", 0x00, 0x8942 },
	{ 18, "iPad1,1",    "k48ap", 0x02, 0x8930 },
	{ 19, "iPad2,1",    "k93ap", 0x04, 0x8940 },
	{ 20, "iPad2,2",    "k94ap", 0x06, 0x8940 },
	{ 21, "iPad2,3",    "k95ap", 0x02, 0x8940 },
	{ 22, "iPad2,4",   "k93aap", 0x06, 0x8942 },
	{ 23, "iPad2,5",   "p105ap", 0x0a, 0x8942 },
	{ 24, "iPad2,6",   "p106ap", 0x0c, 0x8942 },
	{ 25, "iPad2,7",   "p107ap", 0x0e, 0x8942 },
	{ 26, "iPad3,1",     "j1ap", 0x00, 0x8945 },
	{ 27, "iPad3,2",     "j2ap", 0x02, 0x8945 },
	{ 28, "iPad3,3",    "j2aap", 0x04, 0x8945 },
	{ 29, "iPad3,4",   "p101ap", 0x00, 0x8955 },
	{ 30, "iPad3,5",   "p102ap", 0x02, 0x8955 },
	{ 31, "iPad3,6",   "p103ap", 0x04, 0x8955 },
	{ 32, "AppleTV2,1", "k66ap", 0x10, 0x8930 },
	{ 33, "AppleTV3,1", "j33ap", 0x08, 0x8942 },
	{ 34, "AppleTV3,2","j33iap", 0x00, 0x8947 },
	{ -1,         NULL,    NULL,   -1,     -1 }
};

void irecv_set_debug_level(int level);
const char* irecv_strerror(irecv_error_t error);
irecv_error_t irecv_open_attempts(irecv_client_t* pclient, unsigned long long ecid, int attempts);
irecv_error_t irecv_open(irecv_client_t* client, unsigned long long ecid);
irecv_error_t irecv_reset(irecv_client_t client);
irecv_error_t irecv_close(irecv_client_t client);
irecv_error_t irecv_receive(irecv_client_t client);
irecv_error_t irecv_send_exploit(irecv_client_t client);
irecv_error_t irecv_execute_script(irecv_client_t client, const char* filename);
irecv_error_t irecv_set_configuration(irecv_client_t client, int configuration);

irecv_error_t irecv_event_subscribe(irecv_client_t client, irecv_event_type type, irecv_event_cb_t callback, void *user_data);
irecv_error_t irecv_event_unsubscribe(irecv_client_t client, irecv_event_type type);

int irecv_control_transfer(irecv_client_t client, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned char *data, uint16_t wLength, unsigned int timeout);
int irecv_bulk_transfer(irecv_client_t client, unsigned char endpoint, unsigned char *data, int length, int *transferred, unsigned int timeout);

irecv_error_t irecv_send_file(irecv_client_t client, const char* filename, int dfuNotifyFinished);
irecv_error_t irecv_send_command(irecv_client_t client, const char* command);
irecv_error_t irecv_send_buffer(irecv_client_t client, unsigned char* buffer, unsigned long length, int dfuNotifyFinished);

irecv_error_t irecv_saveenv(irecv_client_t client);
irecv_error_t irecv_getret(irecv_client_t client, unsigned int* value);
irecv_error_t irecv_getenv(irecv_client_t client, const char* variable, char** value);
irecv_error_t irecv_setenv(irecv_client_t client, const char* variable, const char* value);
irecv_error_t irecv_set_interface(irecv_client_t client, int interface, int alt_interface);
irecv_error_t irecv_get_cpid(irecv_client_t client, unsigned int* cpid);
irecv_error_t irecv_get_bdid(irecv_client_t client, unsigned int* bdid);
irecv_error_t irecv_get_ecid(irecv_client_t client, unsigned long long* ecid);
irecv_error_t irecv_get_nonce(irecv_client_t client, unsigned char** nonce, int* nonce_size);
irecv_error_t irecv_get_srnm(irecv_client_t client, char* srnm);
irecv_error_t irecv_get_imei(irecv_client_t client, char* imei);

void irecv_hexdump(unsigned char* buf, unsigned int len, unsigned int addr);

void irecv_init();
void irecv_exit();
irecv_client_t irecv_reconnect(irecv_client_t client, int initial_pause);
irecv_error_t irecv_reset_counters(irecv_client_t client);
irecv_error_t irecv_finish_transfer(irecv_client_t client);
irecv_error_t irecv_recv_buffer(irecv_client_t client, char* buffer, unsigned long length);
irecv_error_t irecv_get_device(irecv_client_t client, irecv_device_t* device);

#ifdef __cplusplus
}
#endif

#endif
