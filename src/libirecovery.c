/*
 * libirecovery.c
 * Communication to iBoot/iBSS on Apple iOS devices via USB
 *
 * Copyright (c) 2011-2023 Nikias Bassen <nikias@gmx.li>
 * Copyright (c) 2012-2020 Martin Szulecki <martin.szulecki@libimobiledevice.org>
 * Copyright (c) 2010 Chronic-Dev Team
 * Copyright (c) 2010 Joshua Hill
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
#include <config.h>
#endif

#include <stdio.h>
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#include <libimobiledevice-glue/collection.h>
#include <libimobiledevice-glue/thread.h>

#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
#include <libusb.h>
#if (defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000102)) || (defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102))
#define HAVE_LIBUSB_HOTPLUG_API 1
#endif
#else
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#endif
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#ifndef sleep
#define sleep(n) Sleep(1000 * n)
#endif
#endif
#endif

#ifdef IRECV_STATIC
  #define IRECV_API
#elif defined(_WIN32)
  #define IRECV_API __declspec( dllexport )
#else
  #if __GNUC__ >= 4
    #define IRECV_API __attribute__((visibility("default")))
  #else
    #define IRECV_API
  #endif
#endif

#include "libirecovery.h"
#include "devicedb.h"

// Reference: https://stackoverflow.com/a/2390626/1806760
// Initializer/finalizer sample for MSVC and GCC/Clang.
// 2010-2016 Joe Lowe. Released into the public domain.

#ifdef __cplusplus
    #define INITIALIZER(f) \
        static void f(void); \
        struct f##_t_ { f##_t_(void) { f(); } }; static f##_t_ f##_; \
        static void f(void)
#elif defined(_MSC_VER)
    #pragma section(".CRT$XCU",read)
    #define INITIALIZER2_(f,p) \
        static void f(void); \
        __declspec(allocate(".CRT$XCU")) void (*f##_)(void) = f; \
        __pragma(comment(linker,"/include:" p #f "_")) \
        static void f(void)
    #ifdef _WIN64
        #define INITIALIZER(f) INITIALIZER2_(f,"")
    #else
        #define INITIALIZER(f) INITIALIZER2_(f,"_")
    #endif
#else
    #define INITIALIZER(f) \
        static void f(void) __attribute__((__constructor__)); \
        static void f(void)
#endif

#ifndef WIN32
struct irecv_async_transfer {
 	uint32_t len;
 #ifdef HAVE_IOKIT
 	kern_return_t ret;
 #else
 #ifndef USE_DUMMY
 	enum libusb_transfer_status ret;
 	/* set by async_cb; ret can't tell, since LIBUSB_TRANSFER_COMPLETED is 0 */
 	int completed;
 #endif
 #endif
};

#endif

#if !defined(USE_DUMMY) && !defined(HAVE_IOKIT) && !defined(_WIN32)
#define HAVE_KIS_LINKS 1
struct kis_link;
#endif

struct irecv_client_private {
	int debug;
	int usb_config;
	int usb_interface;
	int usb_alt_interface;
	unsigned int mode;
	int isKIS;
	struct irecv_device_info device_info;
#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
	libusb_device_handle* handle;
#else
	IOUSBDeviceInterface320 **handle;
	IOUSBInterfaceInterface300 **usbInterface;
	CFRunLoopSourceRef async_event_source;
#endif
#else
	HANDLE handle;
#endif
	irecv_event_cb_t progress_callback;
	irecv_event_cb_t received_callback;
	irecv_event_cb_t connected_callback;
	irecv_event_cb_t precommand_callback;
	irecv_event_cb_t postcommand_callback;
	irecv_event_cb_t disconnected_callback;
#ifdef HAVE_KIS_LINKS
	/* Opened through a Debug USB link: KIS DFU, or, with rsm_session set, a
	 * recovery device tunnelled over RSM. */
	struct kis_link *kis;
	unsigned int rsm_session;
#endif
#endif
};

#define USB_TIMEOUT 10000
#define APPLE_VENDOR_ID 0x05AC

// KIS
#define KIS_PRODUCT_ID 0x1881

#define KIS_PORTAL_CONFIG 0x01
#define KIS_PORTAL_RSM    0x10

#define KIS_INDEX_UPLOAD   0x0D
#define KIS_INDEX_ENABLE_A 0x0A // macOS writes to this
#define KIS_INDEX_ENABLE_B 0x14 // macOS writes to this
#define KIS_INDEX_GET_INFO 0x100
#define KIS_INDEX_BOOT_IMG 0x103

#define KIS_ENABLE_A_VAL   0x21 // Value to write to KIS_INDEX_ENABLE_A
#define KIS_ENABLE_B_VAL   0x01 // Value to write to KIS_INDEX_ENABLE_B

#define BUFFER_SIZE 0x1000
#define debug(...) if (libirecovery_debug) fprintf(stderr, __VA_ARGS__)

static int libirecovery_debug = 0;
#ifdef HAVE_KIS_LINKS
static struct collection kis_links;
static mutex_t kis_links_mutex;
/* The device event thread runs and takes over links kept across a stage change. */
static int kis_links_retained;
static int rsm_control(struct kis_link *link, unsigned int session, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int timeout);
static int rsm_bulk(struct kis_link *link, unsigned int session, unsigned char endpoint, unsigned char *data, int length, int *transferred, unsigned int timeout);
static int rsm_string(struct kis_link *link, unsigned int session, uint8_t index, unsigned char *buffer, int size);
#endif
#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
static libusb_context* libirecovery_context = NULL;
#endif
#endif
#endif

#ifndef USE_DUMMY
static unsigned int crc32_lookup_t1[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
	0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
	0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
	0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
	0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
	0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
	0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
	0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
	0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
	0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
	0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
	0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
	0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
	0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
	0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
	0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
	0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
	0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

#define crc32_step(a,b) \
	a = (crc32_lookup_t1[(a & 0xFF) ^ ((unsigned char)b)] ^ (a >> 8))

#ifdef _WIN32
#pragma pack(1)
typedef struct {
	uint16_t vid;
	uint16_t pid;
	uint32_t unk;
	char nonces[255];
	char serial[255];
	char manufacturer[255];
	char product[255];
} KIS_device_info;

typedef struct {
	uint8_t data[0x4000];
	uint32_t size;
	uint32_t unused;
	uint64_t address;
} KIS_upload_chunk;
#pragma pack()
#else
#pragma pack(1)
typedef struct {
	uint16_t sequence;         // A sequence number
	uint8_t  version;          // Protocol version
	uint8_t  portal;           // The "portal" to connect to
	uint8_t  argCount;         // Number of arguments
	uint8_t  indexLo;          // An index
	uint8_t  indexHiRplSizeLo; // High 2 bits of index + low 6 bytes of reply size
	uint8_t  rplSizeHi;        // Reply size high bits, number of words the device should send
	uint32_t reqSize;          // Size of the complete request, including the arguments and payload, excluding the header
	// Followed by arguments and payload data
} KIS_req_header;

typedef struct {
	KIS_req_header hdr;
	uint32_t value;
} KIS_config_wr32;

typedef struct {
  uint8_t  bLength            ; ///< Size of this descriptor in bytes.
  uint8_t  bDescriptorType    ; ///< DEVICE Descriptor Type.
  uint16_t bcdUSB             ; ///< BUSB Specification Release Number in Binary-Coded Decimal (i.e., 2.10 is 210H). This field identifies the release of the USB Specification with which the device and its descriptors are compliant.

  uint8_t  bDeviceClass       ; ///< Class code (assigned by the USB-IF). \li If this field is reset to zero, each interface within a configuration specifies its own class information and the various interfaces operate independently. \li If this field is set to a value between 1 and FEH, the device supports different class specifications on different interfaces and the interfaces may not operate independently. This value identifies the class definition used for the aggregate interfaces. \li If this field is set to FFH, the device class is vendor-specific.
  uint8_t  bDeviceSubClass    ; ///< Subclass code (assigned by the USB-IF). These codes are qualified by the value of the bDeviceClass field. \li If the bDeviceClass field is reset to zero, this field must also be reset to zero. \li If the bDeviceClass field is not set to FFH, all values are reserved for assignment by the USB-IF.
  uint8_t  bDeviceProtocol    ; ///< Protocol code (assigned by the USB-IF). These codes are qualified by the value of the bDeviceClass and the bDeviceSubClass fields. If a device supports class-specific protocols on a device basis as opposed to an interface basis, this code identifies the protocols that the device uses as defined by the specification of the device class. \li If this field is reset to zero, the device does not use class-specific protocols on a device basis. However, it may use classspecific protocols on an interface basis. \li If this field is set to FFH, the device uses a vendor-specific protocol on a device basis.
  uint8_t  bMaxPacketSize0    ; ///< Maximum packet size for endpoint zero (only 8, 16, 32, or 64 are valid). For HS devices is fixed to 64.

  uint16_t idVendor           ; ///< Vendor ID (assigned by the USB-IF).
  uint16_t idProduct          ; ///< Product ID (assigned by the manufacturer).
  uint16_t bcdDevice          ; ///< Device release number in binary-coded decimal.
  uint8_t  iManufacturer      ; ///< Index of string descriptor describing manufacturer.
  uint8_t  iProduct           ; ///< Index of string descriptor describing product.
  uint8_t  iSerialNumber      ; ///< Index of string descriptor describing the device's serial number.

  uint8_t  bNumConfigurations ; ///< Number of possible configurations.
} usb_device_descriptor;

typedef struct {
	KIS_req_header hdr;
	union {
		struct {
			uint32_t              tag;
			uint32_t              unk1;
			uint32_t              maxUploadSize;
			uint32_t              maxDownloadSize; // maybe???
			uint64_t              rambase;
			uint32_t              nonceOffset;
			uint32_t              pad;
			uint8_t               unkpad[0x20];
			usb_device_descriptor deviceDescriptor;
		};
		uint8_t deviceInfo[0x300];
	};
	uint32_t rspsize;
	uint32_t statuscode;
} KIS_device_info;

typedef struct {
	KIS_req_header hdr;
	uint64_t address;
	uint32_t size;
	uint8_t data[0x4000];
} KIS_upload_chunk;

typedef struct {
	KIS_req_header hdr;
	uint32_t size; // Number of bytes read/written
	uint32_t status;
} KIS_generic_reply;
#pragma pack()
#endif

#pragma pack(1)
typedef struct {
	uint16_t cmdcode;
#define MSG_VERSION_QUERY   0x000
#define MSG_ECHO            0x801
#define MSG_DUMP_BUFFER     0x802
#define MSG_SEND_COMMAND    0x803
#define MSG_READ_FILE       0x804
#define MSG_SEND_FILE       0x805
#define MSG_CRC             0x807
#define MSG_ACK             0x808
#define MSG_REJECT          0x809
	uint16_t magic;	    // always 0x1234
	uint32_t size;
	uint32_t loadaddr;  // 0x0 for commands, 0x09000000 for files
} legacyCMD;
#pragma pack()

static THREAD_T th_event_handler = THREAD_T_NULL;
struct collection listeners;
static mutex_t listener_mutex;
struct collection devices;
static mutex_t device_mutex;
#ifndef _WIN32
#ifdef HAVE_IOKIT
static CFRunLoopRef iokit_runloop = NULL;
#else
static libusb_context* irecv_hotplug_ctx = NULL;
#endif
#endif

static void _irecv_deinit(void)
{
	irecv_devicedb_cleanup();
#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
	if (libirecovery_context != NULL) {
		libusb_exit(libirecovery_context);
		libirecovery_context = NULL;
	}
#endif
#endif
	collection_free(&listeners);
	mutex_destroy(&listener_mutex);
#endif
}

INITIALIZER(_irecv_init)
{
	irecv_devicedb_init();
	char* dbglvl = getenv("LIBIRECOVERY_DEBUG_LEVEL");
	if (dbglvl) {
		libirecovery_debug = strtol(dbglvl, NULL, 0);
		irecv_set_debug_level(libirecovery_debug);
	}
#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
	libusb_init(&libirecovery_context);
#endif
#endif
	collection_init(&listeners);
	mutex_init(&listener_mutex);
#ifdef HAVE_KIS_LINKS
	collection_init(&kis_links);
	mutex_init(&kis_links_mutex);
#endif
#endif
	atexit(_irecv_deinit);
}

#ifdef HAVE_IOKIT
static int iokit_get_string_descriptor_ascii(irecv_client_t client, uint8_t desc_index, unsigned char * buffer, int size)
{
	IOReturn result;
	IOUSBDevRequest request;
	unsigned char descriptor[256];

	request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
	request.bRequest = kUSBRqGetDescriptor;
	request.wValue = (kUSBStringDesc << 8); // | desc_index;
	request.wIndex = 0; // All languages 0x409; // language
	request.wLength = sizeof(descriptor) - 1;
	request.pData = descriptor;
	request.wLenDone = 0;

	result = (*client->handle)->DeviceRequest(client->handle, &request);
	if (result == kIOReturnNoDevice)
		return IRECV_E_NO_DEVICE;
	if (result == kIOReturnNotOpen)
		return IRECV_E_USB_STATUS;
	if (result != kIOReturnSuccess)
		return IRECV_E_UNKNOWN_ERROR;

	if (descriptor[0] >= 4) { // && descriptor[2] == 0x9 && descriptor[3] == 0x4) {

		request.wValue = (kUSBStringDesc << 8) | desc_index;
		request.wIndex = descriptor[2] + (descriptor[3] << 8);
		request.wLenDone = 0;
		result = (*client->handle)->DeviceRequest(client->handle, &request);

		if (result == kIOReturnNoDevice)
			return IRECV_E_NO_DEVICE;
		if (result == kIOReturnNotOpen)
			return IRECV_E_USB_STATUS;
		if (result != kIOReturnSuccess)
			return IRECV_E_UNKNOWN_ERROR;

		int i = 2, j = 0;
		for ( ; i < descriptor[0]; i += 2, j += 1) {
			buffer[j] = descriptor[i];
		}
		buffer[j] = 0;

		return request.wLenDone;
	}
	return IRECV_E_UNKNOWN_ERROR;
}
#endif

#if !defined(_WIN32) && !defined(HAVE_IOKIT)
/* Fetch a string descriptor with libirecovery's own USB_TIMEOUT and a few
 * retries. libusb_get_string_descriptor_ascii() hardcodes a 1 s timeout, which
 * some parts miss: the Apple T1/iBridge in recovery answers this control
 * transfer only intermittently and well beyond 1 s, returning
 * LIBUSB_ERROR_TIMEOUT (-7). Losing that read costs us the device identity AND
 * the AP/SEP nonces, so it is worth waiting for. */
static int irecv_get_string_descriptor_ascii_slow(irecv_client_t client, uint8_t desc_index, unsigned char *buffer, int size)
{
	unsigned char data[256];
	int attempt, di, si, ret = -1;

	for (attempt = 0; attempt < 3; attempt++) {
		memset(data, 0, sizeof(data));
		memset(buffer, 0, size);

		ret = irecv_usb_control_transfer(client, 0x80, 0x06, (0x03 << 8) | desc_index, 0,
		                                 data, sizeof(data) - 1, USB_TIMEOUT);
		if (ret < 0) {
			debug("%s: attempt %d: control transfer failed (%d)\n", __func__, attempt + 1, ret);
			continue;
		}
		if (data[1] != 0x03 || data[0] > ret) {
			debug("%s: attempt %d: malformed descriptor\n", __func__, attempt + 1);
			ret = -1;
			continue;
		}
		for (di = 0, si = 2; si < data[0]; si += 2) {
			if (di >= (size - 1)) break;
			buffer[di++] = data[si + 1] ? '?' : data[si];
		}
		buffer[di] = 0;
		debug("%s: got %d bytes on attempt %d\n", __func__, di, attempt + 1);
		return di;
	}
	return ret;
}
#endif

static int irecv_get_string_descriptor_ascii(irecv_client_t client, uint8_t desc_index, unsigned char * buffer, int size)
{
#ifdef HAVE_KIS_LINKS
	if (client->rsm_session) {
		return rsm_string(client->kis, client->rsm_session, desc_index, buffer, size);
	}
#endif
#ifndef _WIN32
#ifdef HAVE_IOKIT
	return iokit_get_string_descriptor_ascii(client, desc_index, buffer, size);
#else
	int r = libusb_get_string_descriptor_ascii(client->handle, desc_index, buffer, size);
	if (r >= 0) return r;
	debug("%s: libusb helper failed (%d); retrying with USB_TIMEOUT\n", __func__, r);
	return irecv_get_string_descriptor_ascii_slow(client, desc_index, buffer, size);
#endif
#else
	irecv_error_t ret;
	unsigned short langid = 0;
	unsigned char data[256];
	int di, si;
	memset(data, 0, sizeof(data));
	memset(buffer, 0, size);

	ret = irecv_usb_control_transfer(client, 0x80, 0x06, (0x03 << 8) | desc_index, langid, data, sizeof(data)-1, USB_TIMEOUT);

	if (ret < 0) return ret;
	if (data[1] != 0x03) return IRECV_E_UNKNOWN_ERROR;
	if (data[0] > ret) return IRECV_E_UNKNOWN_ERROR;

	for (di = 0, si = 2; si < data[0]; si += 2) {
		if (di >= (size - 1)) break;
		if (data[si + 1]) {
			/* high byte */
			buffer[di++] = '?';
		} else {
			buffer[di++] = data[si];
		}
	}
	buffer[di] = 0;

	return di;
#endif
}

static void irecv_load_device_info_from_iboot_string(irecv_client_t client, const char* iboot_string)
{
	if (!client || !iboot_string) {
		return;
	}

	memset(&client->device_info, 0, sizeof(struct irecv_device_info));

	client->device_info.serial_string = strdup(iboot_string);

	char* ptr;

	ptr = strstr(iboot_string, "CPID:");
	if (ptr != NULL) {
		sscanf(ptr, "CPID:%x", &client->device_info.cpid);
		client->device_info.have_cpid = 1;
	} else if (iboot_string[0] != '\0') {
		// early iOS 1 doesn't identify itself. Only assume that when the
		// device actually said something; an EMPTY string means the
		// descriptor read failed, and silently claiming to be an 0x8900
		// part then mis-identifies every modern device (e.g. an Apple T1
		// reports CPID:8002 but would come back as an iPhone 2G).
		client->device_info.cpid = 0x8900;
	}

	ptr = strstr(iboot_string, "CPRV:");
	if (ptr != NULL) {
		sscanf(ptr, "CPRV:%x", &client->device_info.cprv);
		client->device_info.have_cprv = 1;
	}

	ptr = strstr(iboot_string, "CPFM:");
	if (ptr != NULL) {
		sscanf(ptr, "CPFM:%x", &client->device_info.cpfm);
		client->device_info.have_cpfm = 1;
	}

	ptr = strstr(iboot_string, "SCEP:");
	if (ptr != NULL) {
		sscanf(ptr, "SCEP:%x", &client->device_info.scep);
		client->device_info.have_scep = 1;
	}

	ptr = strstr(iboot_string, "BDID:");
	if (ptr != NULL) {
		uint64_t bdid = 0;
		sscanf(ptr, "BDID:%" SCNx64, &bdid);
		client->device_info.bdid = (unsigned int)bdid;
		client->device_info.have_bdid = 1;
	}

	ptr = strstr(iboot_string, "ECID:");
	if (ptr != NULL) {
		sscanf(ptr, "ECID:%" SCNx64, &client->device_info.ecid);
		client->device_info.have_ecid = 1;
	}

	ptr = strstr(iboot_string, "IBFL:");
	if (ptr != NULL) {
		sscanf(ptr, "IBFL:%x", &client->device_info.ibfl);
		client->device_info.have_ibfl = 1;
	}

	char tmp[256];
	tmp[0] = '\0';
	ptr = strstr(iboot_string, "SRNM:[");
	if (ptr != NULL) {
		sscanf(ptr, "SRNM:[%s]", tmp);
		ptr = strrchr(tmp, ']');
		if (ptr != NULL) {
			*ptr = '\0';
		}
		client->device_info.srnm = strdup(tmp);
	}

	tmp[0] = '\0';
	ptr = strstr(iboot_string, "IMEI:[");
	if (ptr != NULL) {
		sscanf(ptr, "IMEI:[%s]", tmp);
		ptr = strrchr(tmp, ']');
		if (ptr != NULL) {
			*ptr = '\0';
		}
		client->device_info.imei = strdup(tmp);
	}

	tmp[0] = '\0';
	ptr = strstr(iboot_string, "SRTG:[");
	if (ptr != NULL) {
		sscanf(ptr, "SRTG:[%s]", tmp);
		ptr = strrchr(tmp, ']');
		if (ptr != NULL) {
			*ptr = '\0';
		}
		client->device_info.srtg = strdup(tmp);
	}

	client->device_info.pid = client->mode;
	if (client->isKIS) {
		client->device_info.pid = KIS_PRODUCT_ID;
	}
}

static void irecv_copy_nonce_with_tag_from_buffer(const char* tag, unsigned char** nonce, unsigned int* nonce_size, const char *buf)
{
	int taglen = strlen(tag);
	int nlen = 0;
	const char* nonce_string = NULL;
	const char* p = buf;
	char* colon = NULL;
	do {
		colon = strchr(p, ':');
		if (!colon)
			break;
		if (colon-taglen < p) {
			break;
		}
		char *space = strchr(colon, ' ');
		if (strncmp(colon-taglen, tag, taglen) == 0) {
			p = colon+1;
			if (!space) {
				nlen = strlen(p);
			} else {
				nlen = space-p;
			}
			nonce_string = p;
			nlen/=2;
			break;
		} else {
			if (!space) {
				break;
			} else {
				p = space+1;
			}
		}
	} while (colon);

	if (nlen == 0) {
		debug("%s: WARNING: couldn't find tag %s in string %s\n", __func__, tag, buf);
		return;
	}

	unsigned char *nn = malloc(nlen);
	if (!nn) {
		return;
	}

	int i = 0;
	for (i = 0; i < nlen; i++) {
		int val = 0;
		if (sscanf(nonce_string+(i*2), "%02X", &val) == 1) {
			nn[i] = (unsigned char)val;
		} else {
			debug("%s: ERROR: unexpected data in nonce result (%2s)\n", __func__, nonce_string+(i*2));
			break;
		}
	}

	if (i != nlen) {
		debug("%s: ERROR: unable to parse nonce\n", __func__);
		free(nn);
		return;
	}

	*nonce = nn;
	*nonce_size = nlen;
}

static void irecv_copy_nonce_with_tag(irecv_client_t client, const char* tag, unsigned char** nonce, unsigned int* nonce_size)
{
	if (!client || !tag) {
		return;
	}

	char buf[256];
	int len = 0;

	*nonce = NULL;
	*nonce_size = 0;

	memset(buf, 0, sizeof(buf));
	len = irecv_get_string_descriptor_ascii(client, 1, (unsigned char*)buf, sizeof(buf)-1);
	if (len < 0) {
		debug("%s: got length: %d\n", __func__, len);
		return;
	}

	buf[len] = 0;

	irecv_copy_nonce_with_tag_from_buffer(tag,nonce,nonce_size,buf);
}

#ifdef HAVE_KIS_LINKS
/*
 * Debug USB links, and iBoot recovery over RSM.
 *
 * A Mac with Debug USB on its port shows one KIS device (05ac:1881) for as long
 * as Debug USB stays on, whatever runs on the Mac. Its doorbell block, RSM
 * portal registers 0x100 to 0x1ff, says what is behind it: the bootrom's DFU
 * (magic "aDFU", the block irecv_kis_load_device_info parses, which holds the
 * device's identity) or RSM channels ("RSM1"). An iBoot with no USB device of
 * its own presents recovery mode as an ordinary iBoot USB device carried by an
 * RSM channel of type 2. In RSM mode the doorbells hold no identity: the
 * tunnelled device's serial can only be read through its channel.
 *
 * A KIS interface can be claimed by one process at a time, and the doorbells
 * can only be read while it is claimed. A process has one link per device,
 * shared by the clients opened on it and by the device event thread, and
 * holds the claim while a client has the link open. The event thread
 * otherwise only looks: it claims the device, reads the doorbells and lets go,
 * passing over a device another process holds (it enables a device once if
 * its doorbells don't read back). It never writes to a channel;
 * a recovery stage is announced with the identity this process last read for
 * the device, from its DFU block or through a client, or not at all.
 *
 * When the last client of a link closes after sending requests that can end
 * the device's stage (uploads, commands), the event thread keeps the claim for
 * a moment. If the stage ends, it keeps it until a client of this process
 * opens the next stage, the device leaves recovery, or a deadline passes, so
 * that no other process opens the next stage's channel in between.
 *
 * RSM 1.x, channel c: configuration in doorbell 0x10+c (type in bits 24-31,
 * ring sizes 0x800 << n with n in bits 4-7 for device to host and 0-3 for host
 * to device), ring addresses in 0x20+2c (host to device) and 0x40+2c, and the
 * producers' offsets in 0x60+c (written by the host) and 0x70+c (by the
 * device): the head in words in bits 0-23, and in bits 24-31 how far the
 * producer has read the other ring, in 1/256ths of it. A new boot stage
 * reinitialises its channel, which clears both offsets. A ring holds commands:
 * a word with the stream in bits 24-31 and the length in bits 0-23, then the
 * payload padded to a word. A recovery channel opens with an empty command on
 * stream 0x80. Stream 0x00 is EP0: the host sends the SETUP, then any OUT data
 * as a command of its own; the device answers an IN request with its data
 * (possibly none), then any request with a one-byte status (0 done, 1
 * stalled). Stream 0x04 is bulk OUT to EP 0x04; as on USB, a transfer ends
 * at a short packet, so an empty command ends one whose length is a multiple
 * of 512.
 */

#define KIS_CMD_PCR 0
#define KIS_CMD_PCW 1
#define KIS_CMD_PAR 2
#define KIS_CMD_PAW 3
#define KIS_HEADER_SIZE 12
#define KIS_TRAILER_SIZE 8
#define KIS_ADDRESS_SPACE 0x0D
#define KIS_MAX_DATA 0x10000
/* Whole packets, so a read for the longest reply cannot overflow. */
#define KIS_LINK_BUFFER (KIS_MAX_DATA + 0x400)
#define KIS_INDEX_PORTALS 0x16
#define KIS_PORTALS_VAL 0x30000
#define KIS_DFU_INFO_SIZE 0x300
/* A look at a device: how long a read may take, and how often to look at a
 * device no client of this process has open (in event thread polls). */
#define KIS_LOOK_TIMEOUT 1000
#define KIS_LOOK_POLLS 5
/* A client open passes over other processes' looks. */
#define KIS_OPEN_ATTEMPTS 10
#define KIS_OPEN_RETRY_MS 50
/* How long the claim is kept after a client that sent stage-ending requests
 * closes, and after the stage then ends. */
#define KIS_LINGER_MS 2000
#define KIS_HANDOFF_MS 60000

#define RSM_INDEX_TIMEOUT 0x0A
#define RSM_TIMEOUT_VAL 288000
#define RSM_DOORBELLS 0x100
#define RSM_DOORBELL_COUNT 256
#define RSM_MAGIC 0x52534D31
#define RSM_TYPE_RECOVERY 2
#define RSM_STREAM_EP0 0x00
#define RSM_STREAM_BULK 0x04
#define RSM_STREAM_OPEN 0x80
#define RSM_BULK_CHUNK 0x8000
#define RSM_RX_BUFFER (KIS_MAX_DATA + 0x100)
#define RSM_CHECK_MS 100

enum kis_state {
	KIS_STATE_FAILED,   /* the doorbells could not be read */
	KIS_STATE_EMPTY,    /* nothing behind them, as between boot stages */
	KIS_STATE_OTHER,    /* neither DFU nor a recovery channel */
	KIS_STATE_DFU,
	KIS_STATE_RECOVERY,
};

/* What the doorbells show. */
struct kis_look {
	enum kis_state state;
	/* KIS_STATE_DFU */
	uint16_t mode;
	char serial[256];
	/* KIS_STATE_RECOVERY: the first recovery channel */
	unsigned int chan;
	uint32_t cfg;
	uint64_t h2d;
	uint64_t d2h;
	uint32_t host;
	uint32_t dev;
};

struct kis_link {
	/* Lifetime: under kis_links_mutex. */
	uint32_t location;
	int refs;
	int clients;
	/* 1: a reference is kept for the event thread across a stage change; 2: it has taken it. */
	int retained;
	/* A client sent a request that can end the device's stage. */
	int drove;
	/* Transport: one KIS request at a time. */
	mutex_t lock;
	int dead; /* set under lock and kis_links_mutex */
	int enabled;
	uint16_t seq;
	uint16_t packet;
	uint8_t *out;
	uint8_t *in;
	libusb_device_handle *handle;
	/* The recovery session, while session is nonzero: changed under xfer and
	 * lock, read under either. xfer: one recovery transfer at a time. */
	mutex_t xfer;
	unsigned int session;
	unsigned int sessions;
	unsigned int chan;
	uint64_t h2d;
	uint64_t d2h;
	uint32_t h2d_len;
	uint32_t d2h_len;
	uint32_t head;
	uint32_t tail;
	uint32_t bell;
	uint32_t dev_head;
	uint32_t dev_cons;
	uint64_t checked;
	uint8_t *rx;
	uint32_t rx_len;
	uint16_t pid;
	char serial[256];
};

static uint32_t kis_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void kis_put32(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static uint64_t kis_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void kis_sleep_ms(unsigned int ms)
{
	usleep(ms * 1000);
}

/* Caller holds link->lock. */
static void kis_link_set_dead(struct kis_link *link)
{
	mutex_lock(&kis_links_mutex);
	link->dead = 1;
	mutex_unlock(&kis_links_mutex);
}

/* Caller holds link->lock. */
static irecv_error_t kis_link_bulk(struct kis_link *link, uint8_t ep, uint8_t *buf, int len, int *done, unsigned int timeout)
{
	int ret = libusb_bulk_transfer(link->handle, ep, buf, len, done, timeout);
	switch (ret) {
	case 0:
		return IRECV_E_SUCCESS;
	case LIBUSB_ERROR_TIMEOUT:
		return IRECV_E_TIMEOUT;
	case LIBUSB_ERROR_NO_DEVICE:
		kis_link_set_dead(link);
		return IRECV_E_NO_DEVICE;
	default:
		debug("%s: endpoint %02x: %s\n", __func__, ep, libusb_error_name(ret));
		if (ret == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(link->handle, ep);
		}
		return IRECV_E_USB_STATUS;
	}
}

/* Send the request built in link->out and read its reply, at most reply_max
 * bytes, into link->in, passing over replies to earlier requests. The device
 * takes a transfer as ended by a short packet, so a request that fills its
 * last packet is followed by an empty one, and a reply that fills its last
 * packet is read by its length. Caller holds link->lock. */
static irecv_error_t kis_link_exchange(struct kis_link *link, uint32_t out_len, uint32_t reply_max, uint32_t *in_len, unsigned int timeout)
{
	uint8_t portal = link->out[3];
	uint8_t ep = (portal == KIS_PORTAL_CONFIG) ? 1 : 3;
	uint16_t seq = ++link->seq;
	uint32_t want = (reply_max + link->packet - 1) / link->packet * link->packet;
	int done = 0;

	if (link->dead) {
		return IRECV_E_NO_DEVICE;
	}
	link->out[0] = seq & 0xFF;
	link->out[1] = seq >> 8;
	irecv_error_t err = kis_link_bulk(link, ep, link->out, (int)out_len, &done, timeout);
	if (err != IRECV_E_SUCCESS) {
		return err;
	}
	if ((uint32_t)done != out_len) {
		return IRECV_E_USB_UPLOAD;
	}
	if (out_len % link->packet == 0) {
		err = kis_link_bulk(link, ep, link->out, 0, &done, timeout);
		if (err != IRECV_E_SUCCESS) {
			return err;
		}
	}
	if (want > KIS_LINK_BUFFER) {
		want = KIS_LINK_BUFFER;
	}
	for (;;) {
		err = kis_link_bulk(link, ep | 0x80, link->in, (int)want, &done, timeout);
		if (err != IRECV_E_SUCCESS) {
			return err;
		}
		if (done >= KIS_HEADER_SIZE + KIS_TRAILER_SIZE && link->in[3] == portal && (link->in[0] | (link->in[1] << 8)) == seq) {
			break;
		}
		debug("%s: passing over a reply for portal %02x (%d bytes)\n", __func__, done > 3 ? link->in[3] : 0, done);
	}
	*in_len = (uint32_t)done;
	return IRECV_E_SUCCESS;
}

/* One KIS request, answered with up to reply_data bytes: the reply's data is
 * at link->in + KIS_HEADER_SIZE, and *size and *status come from its trailer.
 * Caller holds link->lock. */
static irecv_error_t kis_link_request(struct kis_link *link, uint8_t portal, uint32_t info, const uint8_t *args, uint32_t args_len, const uint8_t *data, uint32_t data_len, uint32_t reply_data, uint32_t *reply_len, uint32_t *size, uint32_t *status, unsigned int timeout)
{
	uint32_t len = args_len + data_len;
	uint32_t in_len = 0;

	if (KIS_HEADER_SIZE + len > KIS_LINK_BUFFER) {
		return IRECV_E_INVALID_INPUT;
	}
	link->out[2] = 0xA0;
	link->out[3] = portal;
	kis_put32(link->out + 4, info);
	kis_put32(link->out + 8, len);
	if (args_len) {
		memcpy(link->out + KIS_HEADER_SIZE, args, args_len);
	}
	if (data_len) {
		memcpy(link->out + KIS_HEADER_SIZE + args_len, data, data_len);
	}
	irecv_error_t err = kis_link_exchange(link, KIS_HEADER_SIZE + len, KIS_HEADER_SIZE + reply_data + KIS_TRAILER_SIZE, &in_len, timeout);
	if (err != IRECV_E_SUCCESS) {
		return err;
	}
	*reply_len = in_len - KIS_HEADER_SIZE - KIS_TRAILER_SIZE;
	*size = kis_get32(link->in + in_len - 8);
	*status = kis_get32(link->in + in_len - 4);
	return IRECV_E_SUCCESS;
}

/* A request built by the KIS DFU code, with its reply copied back whole. */
static irecv_error_t kis_link_raw(struct kis_link *link, const void *req, size_t req_size, void *rpl, size_t *rpl_size)
{
	uint32_t in_len = 0;

	if (req_size < KIS_HEADER_SIZE || req_size > KIS_LINK_BUFFER) {
		return IRECV_E_INVALID_INPUT;
	}
	mutex_lock(&link->lock);
	memcpy(link->out, req, req_size);
	irecv_error_t err = kis_link_exchange(link, (uint32_t)req_size, (uint32_t)*rpl_size, &in_len, USB_TIMEOUT);
	if (err == IRECV_E_SUCCESS) {
		if (in_len < *rpl_size) {
			*rpl_size = in_len;
		}
		memcpy(rpl, link->in, *rpl_size);
	}
	mutex_unlock(&link->lock);
	return err;
}

/* Caller holds link->lock. */
static irecv_error_t kis_link_write32(struct kis_link *link, uint8_t portal, uint16_t index, uint32_t value)
{
	uint8_t arg[4];
	uint32_t reply_len, size, status;

	kis_put32(arg, value);
	return kis_link_request(link, portal, KIS_CMD_PCW | ((uint32_t)index << 8) | (1 << 18), arg, 4, NULL, 0, 0, &reply_len, &size, &status, USB_TIMEOUT);
}

/* Caller holds link->lock. */
static irecv_error_t rsm_read_regs(struct kis_link *link, uint16_t index, uint32_t count, uint32_t *words, uint32_t *got, unsigned int timeout)
{
	uint32_t reply_len, size, status, i;

	irecv_error_t err = kis_link_request(link, KIS_PORTAL_RSM, KIS_CMD_PCR | ((uint32_t)index << 8) | (count << 18), NULL, 0, NULL, 0, 4 * count, &reply_len, &size, &status, timeout);
	if (err != IRECV_E_SUCCESS) {
		return err;
	}
	*got = reply_len / 4;
	if (*got > count) {
		*got = count;
	}
	for (i = 0; i < *got; i++) {
		words[i] = kis_get32(link->in + KIS_HEADER_SIZE + 4 * i);
	}
	return IRECV_E_SUCCESS;
}

/* Caller holds link->lock. */
static irecv_error_t rsm_access(struct kis_link *link, int write, uint64_t address, uint8_t *data, uint32_t len)
{
	uint8_t args[12];
	uint32_t reply_len, size, status;

	kis_put32(args, (uint32_t)address);
	kis_put32(args + 4, (uint32_t)(address >> 32));
	kis_put32(args + 8, len);
	irecv_error_t err = kis_link_request(link, KIS_PORTAL_RSM, (write ? KIS_CMD_PAW : KIS_CMD_PAR) | (KIS_ADDRESS_SPACE << 8), args, sizeof(args), write ? data : NULL, write ? len : 0, write ? 0 : len, &reply_len, &size, &status, USB_TIMEOUT);
	if (err != IRECV_E_SUCCESS) {
		return err;
	}
	if (size != len || (!write && reply_len < len)) {
		debug("%s: %s %u bytes at %#" PRIx64 ": %u done, status %#x\n", __func__, write ? "write" : "read", len, address, size, status);
		return IRECV_E_USB_STATUS;
	}
	if (!write) {
		memcpy(data, link->in + KIS_HEADER_SIZE, len);
	}
	return IRECV_E_SUCCESS;
}

/* Read the device's offset doorbell. Caller holds link->lock. */
static irecv_error_t rsm_poll(struct kis_link *link)
{
	uint32_t w = 0, got = 0;

	irecv_error_t err = rsm_read_regs(link, RSM_DOORBELLS + 0x70 + link->chan, 1, &w, &got, USB_TIMEOUT);
	if (err != IRECV_E_SUCCESS) {
		return err;
	}
	if (got != 1 || ((w & 0xFFFFFF) << 2) >= link->d2h_len) {
		link->session = 0;
		return IRECV_E_NO_DEVICE;
	}
	link->dev_head = (w & 0xFFFFFF) << 2;
	link->dev_cons = (w >> 24) * (link->h2d_len >> 8);
	return IRECV_E_SUCCESS;
}

/* Whether the channel is still the one we opened: a new boot stage
 * reinitialises it, which clears our offset doorbell. Caller holds link->lock. */
static int rsm_alive(struct kis_link *link)
{
	uint32_t w = 0, got = 0;

	link->checked = kis_now_ms();
	if (rsm_read_regs(link, RSM_DOORBELLS + 0x60 + link->chan, 1, &w, &got, USB_TIMEOUT) != IRECV_E_SUCCESS || got != 1 || w != link->bell) {
		debug("%s: channel %u reset\n", __func__, link->chan);
		link->session = 0;
		return 0;
	}
	return 1;
}

/* The longest command payload the host to device ring can take. Caller holds
 * link->lock. */
static uint32_t rsm_max_payload(struct kis_link *link)
{
	uint32_t room = link->h2d_len - (link->h2d_len >> 8);
	if (room > KIS_MAX_DATA) {
		room = KIS_MAX_DATA;
	}
	return (room - 4) & ~3U;
}

/* Queue one command for the device. Caller holds link->xfer. */
static irecv_error_t rsm_send(struct kis_link *link, unsigned int session, uint8_t stream, const uint8_t *data, uint32_t len, unsigned int timeout)
{
	uint32_t total = 4 + ((len + 3) & ~3U);
	uint64_t start = kis_now_ms();
	unsigned int wait = 1;
	irecv_error_t err = IRECV_E_SUCCESS;

	if (len > KIS_MAX_DATA) {
		return IRECV_E_INVALID_INPUT;
	}
	uint8_t *cmd = calloc(1, total);
	if (!cmd) {
		return IRECV_E_OUT_OF_MEMORY;
	}
	kis_put32(cmd, ((uint32_t)stream << 24) | len);
	if (len) {
		memcpy(cmd + 4, data, len);
	}

	mutex_lock(&link->lock);
	for (;;) {
		if (link->session != session) {
			err = IRECV_E_NO_DEVICE;
			break;
		}
		if (len > rsm_max_payload(link)) {
			err = IRECV_E_INVALID_INPUT;
			break;
		}
		/* A stage that ended while we were idle must not get our next write. */
		uint64_t now = kis_now_ms();
		if (now - link->checked >= RSM_CHECK_MS && !rsm_alive(link)) {
			err = IRECV_E_NO_DEVICE;
			break;
		}
		uint32_t granule = link->h2d_len >> 8;
		uint32_t used = (link->head + link->h2d_len - link->dev_cons) % link->h2d_len;
		if (used + total + granule <= link->h2d_len) {
			break;
		}
		uint32_t before = link->dev_cons;
		err = rsm_poll(link);
		if (err != IRECV_E_SUCCESS) {
			break;
		}
		if (link->dev_cons != before) {
			wait = 1;
			continue;
		}
		if (now - start > timeout) {
			err = IRECV_E_TIMEOUT;
			break;
		}
		mutex_unlock(&link->lock);
		kis_sleep_ms(wait);
		if (wait < 8) {
			wait *= 2;
		}
		mutex_lock(&link->lock);
	}
	if (err == IRECV_E_SUCCESS) {
		uint32_t off = link->head;
		uint32_t first = link->h2d_len - off;
		if (first > total) {
			first = total;
		}
		err = rsm_access(link, 1, link->h2d + off, cmd, first);
		if (err == IRECV_E_SUCCESS && first < total) {
			err = rsm_access(link, 1, link->h2d, cmd + first, total - first);
		}
		if (err == IRECV_E_SUCCESS) {
			uint8_t bell[4];
			uint32_t reply_len, size, status;
			link->head = (off + total) % link->h2d_len;
			link->bell = (link->head >> 2) | ((link->tail / (link->d2h_len >> 8)) << 24);
			kis_put32(bell, link->bell);
			err = kis_link_request(link, KIS_PORTAL_RSM, KIS_CMD_PCW | ((uint32_t)(RSM_DOORBELLS + 0x60 + link->chan) << 8) | (1 << 18), bell, 4, NULL, 0, 0, &reply_len, &size, &status, USB_TIMEOUT);
			if (err == IRECV_E_SUCCESS && size != 4) {
				err = IRECV_E_USB_STATUS;
			}
		}
		if (err != IRECV_E_SUCCESS) {
			debug("%s: stream %02x: write failed, error %d\n", __func__, stream, err);
			link->session = 0;
		}
	}
	mutex_unlock(&link->lock);
	free(cmd);
	return err;
}

/* The next command from the device on any stream. Caller holds link->xfer. */
static irecv_error_t rsm_recv(struct kis_link *link, unsigned int session, uint8_t *stream, uint8_t *buf, uint32_t cap, uint32_t *len, unsigned int timeout)
{
	uint64_t start = kis_now_ms();
	unsigned int wait = 1;
	irecv_error_t err = IRECV_E_SUCCESS;

	mutex_lock(&link->lock);
	for (;;) {
		if (link->session != session) {
			err = IRECV_E_NO_DEVICE;
			break;
		}
		if (link->rx_len >= 4) {
			uint32_t word = kis_get32(link->rx);
			uint32_t n = word & 0xFFFFFF;
			uint32_t total = 4 + ((n + 3) & ~3U);
			if (total > RSM_RX_BUFFER) {
				err = IRECV_E_USB_STATUS;
				link->session = 0;
				break;
			}
			if (link->rx_len >= total) {
				*stream = word >> 24;
				*len = n;
				memcpy(buf, link->rx + 4, n < cap ? n : cap);
				link->rx_len -= total;
				memmove(link->rx, link->rx + total, link->rx_len);
				break;
			}
		}
		if (link->tail != link->dev_head) {
			uint32_t n = (link->dev_head + link->d2h_len - link->tail) % link->d2h_len;
			if (n > link->d2h_len - link->tail) {
				n = link->d2h_len - link->tail;
			}
			if (n > RSM_RX_BUFFER - link->rx_len) {
				n = RSM_RX_BUFFER - link->rx_len;
			}
			if (n > KIS_MAX_DATA) {
				n = KIS_MAX_DATA;
			}
			err = rsm_access(link, 0, link->d2h + link->tail, link->rx + link->rx_len, n);
			if (err != IRECV_E_SUCCESS) {
				link->session = 0;
				break;
			}
			link->rx_len += n;
			link->tail = (link->tail + n) % link->d2h_len;
			continue;
		}
		err = rsm_poll(link);
		if (err != IRECV_E_SUCCESS) {
			break;
		}
		if (link->tail != link->dev_head) {
			wait = 1;
			continue;
		}
		uint64_t now = kis_now_ms();
		if (now - start > timeout) {
			err = IRECV_E_TIMEOUT;
			break;
		}
		if (now - link->checked >= RSM_CHECK_MS && !rsm_alive(link)) {
			err = IRECV_E_NO_DEVICE;
			break;
		}
		mutex_unlock(&link->lock);
		kis_sleep_ms(wait);
		if (wait < 8) {
			wait *= 2;
		}
		mutex_lock(&link->lock);
	}
	mutex_unlock(&link->lock);
	return err;
}

static irecv_error_t rsm_recv_ep0(struct kis_link *link, unsigned int session, uint8_t *buf, uint32_t cap, uint32_t *len, unsigned int timeout)
{
	for (;;) {
		uint8_t stream = 0;
		irecv_error_t err = rsm_recv(link, session, &stream, buf, cap, len, timeout);
		if (err != IRECV_E_SUCCESS || stream == RSM_STREAM_EP0) {
			return err;
		}
		debug("%s: passing over %u bytes on stream %02x\n", __func__, *len, stream);
	}
}

/* End the session after an EP0 exchange went wrong: a reply that arrives late
 * would otherwise be taken as the next request's. */
static void rsm_drop(struct kis_link *link, unsigned int session)
{
	mutex_lock(&link->lock);
	if (link->session == session) {
		link->session = 0;
	}
	mutex_unlock(&link->lock);
}

/* EP0 on the tunnelled device. Caller holds link->xfer. */
static int rsm_control_locked(struct kis_link *link, unsigned int session, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int timeout)
{
	uint8_t setup[8] = { bm_request_type, b_request, w_value & 0xFF, w_value >> 8, w_index & 0xFF, w_index >> 8, w_length & 0xFF, w_length >> 8 };
	int in = (bm_request_type & 0x80) != 0;
	uint8_t none[1];
	uint32_t n = 0, got = 0;
	uint8_t status = 0xFF;

	if (w_length > 0 && !data) {
		return IRECV_E_INVALID_INPUT;
	}
	irecv_error_t err = rsm_send(link, session, RSM_STREAM_EP0, setup, sizeof(setup), timeout);
	if (err == IRECV_E_SUCCESS && !in && w_length > 0) {
		err = rsm_send(link, session, RSM_STREAM_EP0, data, w_length, timeout);
	}
	if (err == IRECV_E_SUCCESS && in) {
		err = rsm_recv_ep0(link, session, w_length ? data : none, w_length, &n, timeout);
		if (n > w_length) {
			n = w_length;
		}
	}
	if (err == IRECV_E_SUCCESS) {
		err = rsm_recv_ep0(link, session, &status, 1, &got, timeout);
		if (err == IRECV_E_SUCCESS && got != 1) {
			err = IRECV_E_USB_STATUS;
		}
	}
	if (err != IRECV_E_SUCCESS) {
		rsm_drop(link, session);
		return err;
	}
	if (status != 0) {
		return IRECV_E_PIPE;
	}
	return in ? (int)n : w_length;
}

static void kis_link_drove(struct kis_link *link)
{
	mutex_lock(&kis_links_mutex);
	link->drove = 1;
	mutex_unlock(&kis_links_mutex);
}

static int rsm_control(struct kis_link *link, unsigned int session, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int timeout)
{
	if (!(bm_request_type & 0x80) && w_length > 0) {
		kis_link_drove(link);
	}
	mutex_lock(&link->xfer);
	int ret = rsm_control_locked(link, session, bm_request_type, b_request, w_value, w_index, data, w_length, timeout);
	mutex_unlock(&link->xfer);
	return ret;
}

static int rsm_bulk(struct kis_link *link, unsigned int session, unsigned char endpoint, unsigned char *data, int length, int *transferred, unsigned int timeout)
{
	irecv_error_t err = IRECV_E_SUCCESS;
	int done = 0;

	if (transferred) {
		*transferred = 0;
	}
	if (endpoint != RSM_STREAM_BULK || length < 0) {
		return IRECV_E_UNSUPPORTED;
	}
	kis_link_drove(link);
	mutex_lock(&link->xfer);
	mutex_lock(&link->lock);
	int chunk = (int)rsm_max_payload(link);
	mutex_unlock(&link->lock);
	if (chunk > RSM_BULK_CHUNK) {
		chunk = RSM_BULK_CHUNK;
	}
	/* The tunnel keeps USB's packet rule: a transfer ends at a short packet,
	 * and an empty command is the zero-length one. */
	if (length == 0) {
		err = rsm_send(link, session, RSM_STREAM_BULK, NULL, 0, timeout);
	}
	while (done < length) {
		int n = length - done;
		if (n > chunk) {
			n = chunk;
		}
		err = rsm_send(link, session, RSM_STREAM_BULK, data + done, (uint32_t)n, timeout);
		if (err != IRECV_E_SUCCESS) {
			break;
		}
		done += n;
	}
	mutex_unlock(&link->xfer);
	if (transferred) {
		*transferred = done;
	}
	return err;
}

/* A string descriptor as ASCII, like libusb_get_string_descriptor_ascii.
 * Caller holds link->xfer. */
static int rsm_string_locked(struct kis_link *link, unsigned int session, uint8_t index, unsigned char *buffer, int size)
{
	uint8_t desc[255];
	int i, j = 0;

	int n = rsm_control_locked(link, session, 0x80, 6, 0x0300, 0, desc, sizeof(desc), USB_TIMEOUT);
	if (n < 4) {
		return n < 0 ? n : IRECV_E_UNKNOWN_ERROR;
	}
	uint16_t langid = desc[2] | (desc[3] << 8);
	n = rsm_control_locked(link, session, 0x80, 6, 0x0300 | index, langid, desc, sizeof(desc), USB_TIMEOUT);
	if (n < 2) {
		return n < 0 ? n : IRECV_E_UNKNOWN_ERROR;
	}
	if (desc[1] != 3) {
		return IRECV_E_UNKNOWN_ERROR;
	}
	if (desc[0] < n) {
		n = desc[0];
	}
	for (i = 2; i + 1 < n && j < size - 1; i += 2) {
		buffer[j++] = desc[i + 1] ? '?' : desc[i];
	}
	buffer[j] = '\0';
	return j;
}

static int rsm_string(struct kis_link *link, unsigned int session, uint8_t index, unsigned char *buffer, int size)
{
	mutex_lock(&link->xfer);
	int ret = rsm_string_locked(link, session, index, buffer, size);
	mutex_unlock(&link->xfer);
	return ret;
}

static int kis_info_string(const uint8_t *info, uint32_t word, char *buf, size_t size)
{
	size_t off = (size_t)word * 4;
	size_t i, j = 0;

	if (off + 2 > KIS_DFU_INFO_SIZE) {
		return 0;
	}
	size_t len = info[off];
	if (info[off + 1] != 3 || len < 2 || off + len > KIS_DFU_INFO_SIZE) {
		return 0;
	}
	for (i = 2; i + 1 < len && j + 1 < size; i += 2) {
		buf[j++] = info[off + i + 1] ? '?' : info[off + i];
	}
	buf[j] = '\0';
	return (int)j;
}

/* Read the doorbells. Writes nothing. */
static irecv_error_t kis_link_look(struct kis_link *link, struct kis_look *look, unsigned int timeout)
{
	uint32_t w[RSM_DOORBELL_COUNT];
	uint8_t info[KIS_DFU_INFO_SIZE];
	uint32_t got = 0;
	unsigned int c, i;

	memset(look, 0, sizeof(*look));
	mutex_lock(&link->lock);
	irecv_error_t err = rsm_read_regs(link, RSM_DOORBELLS, RSM_DOORBELL_COUNT, w, &got, timeout);
	mutex_unlock(&link->lock);
	if (err != IRECV_E_SUCCESS) {
		look->state = KIS_STATE_FAILED;
		return err;
	}
	if (got < RSM_DOORBELL_COUNT) {
		look->state = KIS_STATE_EMPTY;
		return IRECV_E_SUCCESS;
	}
	look->state = KIS_STATE_OTHER;
	if (w[0] != RSM_MAGIC) {
		for (i = 0; i < KIS_DFU_INFO_SIZE / 4; i++) {
			kis_put32(info + 4 * i, w[i]);
		}
		if (kis_info_string(info, info[64 + 16], look->serial, sizeof(look->serial)) && strstr(look->serial, "CPID:")) {
			look->mode = info[64 + 10] | (info[64 + 11] << 8);
			look->state = KIS_STATE_DFU;
		}
		return IRECV_E_SUCCESS;
	}
	/* Only the 1.x layout is known. */
	if ((w[1] >> 16) != 1) {
		return IRECV_E_SUCCESS;
	}
	for (c = 0; c < 16; c++) {
		if ((w[0x10 + c] >> 24) == RSM_TYPE_RECOVERY) {
			look->state = KIS_STATE_RECOVERY;
			look->chan = c;
			look->cfg = w[0x10 + c];
			look->h2d = w[0x20 + 2 * c] | ((uint64_t)w[0x21 + 2 * c] << 32);
			look->d2h = w[0x40 + 2 * c] | ((uint64_t)w[0x41 + 2 * c] << 32);
			look->host = w[0x60 + c];
			look->dev = w[0x70 + c];
			break;
		}
	}
	return IRECV_E_SUCCESS;
}

/* Whether the link's session is the stage the doorbells show. */
static int kis_link_current(struct kis_link *link, const struct kis_look *look)
{
	mutex_lock(&link->lock);
	int current = link->session && look->state == KIS_STATE_RECOVERY && link->chan == look->chan && link->bell == look->host;
	mutex_unlock(&link->lock);
	return current;
}

/* Open the recovery channel the doorbells show, read just now, and read the
 * device behind it. Caller holds link->xfer. */
static irecv_error_t rsm_attach(struct kis_link *link, const struct kis_look *look, unsigned int *psession)
{
	uint8_t desc[18];
	char serial[256];
	unsigned int session;

	*psession = 0;
	mutex_lock(&link->lock);
	link->chan = look->chan;
	link->h2d_len = 0x800U << (look->cfg & 0xF);
	link->d2h_len = 0x800U << ((look->cfg >> 4) & 0xF);
	link->h2d = look->h2d;
	link->d2h = look->d2h;
	link->bell = look->host;
	link->head = (look->host & 0xFFFFFF) << 2;
	/* Pass over anything the device queued before we came. */
	link->tail = link->dev_head = (look->dev & 0xFFFFFF) << 2;
	link->dev_cons = (look->dev >> 24) * (link->h2d_len >> 8);
	link->rx_len = 0;
	link->checked = kis_now_ms();
	if (++link->sessions == 0) {
		link->sessions = 1;
	}
	session = link->sessions;
	link->session = (link->head < link->h2d_len && link->tail < link->d2h_len) ? session : 0;
	int ok = (link->session == session);
	mutex_unlock(&link->lock);
	if (!ok) {
		return IRECV_E_UNKNOWN_ERROR;
	}

	debug("%s: channel %u, rings %#" PRIx64 "/%#x and %#" PRIx64 "/%#x, offsets %#x and %#x\n", __func__, look->chan, look->h2d, 0x800U << (look->cfg & 0xF), look->d2h, 0x800U << ((look->cfg >> 4) & 0xF), look->host, look->dev);
	uint16_t pid = 0;
	irecv_error_t err = rsm_send(link, session, RSM_STREAM_OPEN, NULL, 0, USB_TIMEOUT);
	if (err == IRECV_E_SUCCESS) {
		int n = rsm_control_locked(link, session, 0x80, 6, 0x0100, 0, desc, sizeof(desc), USB_TIMEOUT);
		uint16_t vid = desc[8] | (desc[9] << 8);
		pid = desc[10] | (desc[11] << 8);
		if (n < 0) {
			err = n;
		} else if (n != sizeof(desc) || desc[1] != 1 || vid != APPLE_VENDOR_ID || pid < IRECV_K_RECOVERY_MODE_1 || pid > IRECV_K_RECOVERY_MODE_4) {
			debug("%s: not a recovery device behind channel %u\n", __func__, look->chan);
			err = IRECV_E_UNSUPPORTED;
		}
	}
	if (err == IRECV_E_SUCCESS) {
		int n = rsm_string_locked(link, session, desc[16], (unsigned char*)serial, sizeof(serial));
		if (n < 0) {
			err = n;
		}
	}
	if (err == IRECV_E_SUCCESS) {
		int n = rsm_control_locked(link, session, 0x00, 9, 1, 0, NULL, 0, USB_TIMEOUT);
		if (n < 0) {
			err = n;
		}
	}
	mutex_lock(&link->lock);
	if (err == IRECV_E_SUCCESS && link->session == session) {
		link->pid = pid;
		memcpy(link->serial, serial, sizeof(link->serial));
		*psession = session;
		debug("%s: %04x:%04x %s\n", __func__, APPLE_VENDOR_ID, link->pid, link->serial);
	} else {
		if (link->session == session) {
			link->session = 0;
		}
		if (err == IRECV_E_SUCCESS) {
			err = IRECV_E_NO_DEVICE;
		}
	}
	mutex_unlock(&link->lock);
	return err;
}

static void kis_link_free(struct kis_link *link)
{
	if (link->handle) {
		libusb_release_interface(link->handle, 0);
		libusb_close(link->handle);
	}
	mutex_destroy(&link->lock);
	mutex_destroy(&link->xfer);
	free(link->out);
	free(link->in);
	free(link->rx);
	free(link);
}

static struct kis_link* kis_link_new(uint32_t location)
{
	struct kis_link *link = calloc(1, sizeof(struct kis_link));
	if (!link) {
		return NULL;
	}
	link->location = location;
	link->refs = 1;
	link->packet = 512;
	mutex_init(&link->lock);
	mutex_init(&link->xfer);
	link->out = malloc(KIS_LINK_BUFFER);
	link->in = malloc(KIS_LINK_BUFFER);
	link->rx = malloc(RSM_RX_BUFFER);
	if (!link->out || !link->in || !link->rx) {
		kis_link_free(link);
		return NULL;
	}
	return link;
}

/* Enable the device as libirecovery always has, plus what Apple's host
 * writes before it reads RSM doorbells. */
static irecv_error_t kis_link_enable(struct kis_link *link)
{
	irecv_error_t err = IRECV_E_SUCCESS;

	mutex_lock(&link->lock);
	if (!link->enabled) {
		err = kis_link_write32(link, KIS_PORTAL_CONFIG, KIS_INDEX_ENABLE_A, KIS_ENABLE_A_VAL);
		if (err == IRECV_E_SUCCESS) {
			err = kis_link_write32(link, KIS_PORTAL_CONFIG, KIS_INDEX_ENABLE_B, KIS_ENABLE_B_VAL);
		}
		if (err == IRECV_E_SUCCESS) {
			err = kis_link_write32(link, KIS_PORTAL_RSM, RSM_INDEX_TIMEOUT, RSM_TIMEOUT_VAL);
		}
		if (err == IRECV_E_SUCCESS) {
			err = kis_link_write32(link, KIS_PORTAL_CONFIG, KIS_INDEX_PORTALS, KIS_PORTALS_VAL);
		}
		link->enabled = (err == IRECV_E_SUCCESS);
	}
	mutex_unlock(&link->lock);
	return err;
}

static void kis_link_put(struct kis_link *link)
{
	if (!link) {
		return;
	}
	mutex_lock(&kis_links_mutex);
	int last = (--link->refs == 0);
	if (last) {
		collection_remove(&kis_links, link);
	}
	mutex_unlock(&kis_links_mutex);
	if (last) {
		kis_link_free(link);
	}
}

/* A client lets go of its link. After requests that can end the device's
 * stage, its reference goes to the event thread instead, which keeps the
 * claim across the stage change (see kis_watch_poll). */
static void kis_link_put_client(struct kis_link *link)
{
	mutex_lock(&kis_links_mutex);
	link->clients--;
	if (link->clients == 0) {
		int keep = link->drove && kis_links_retained && !link->retained && !link->dead;
		link->drove = 0;
		if (keep) {
			link->retained = 1;
			mutex_unlock(&kis_links_mutex);
			return;
		}
	}
	mutex_unlock(&kis_links_mutex);
	kis_link_put(link);
}

/* This process's link to the device at location, with a reference. */
static struct kis_link* kis_link_find(uint32_t location)
{
	struct kis_link *link = NULL;

	mutex_lock(&kis_links_mutex);
	FOREACH(struct kis_link *l, &kis_links) {
		if (!l->dead && l->location == location) {
			l->refs++;
			link = l;
			break;
		}
	} ENDFOREACH
	mutex_unlock(&kis_links_mutex);
	return link;
}

/* Open and claim the KIS device at bus and address, once. */
static libusb_device_handle* kis_link_claim(uint8_t bus, uint8_t address, int *err)
{
	libusb_device **list = NULL;
	libusb_device_handle *handle = NULL;
	int i, count, ret = LIBUSB_ERROR_NO_DEVICE;

	count = libusb_get_device_list(libirecovery_context, &list);
	for (i = 0; i < count; i++) {
		if (libusb_get_bus_number(list[i]) == bus && libusb_get_device_address(list[i]) == address) {
			ret = libusb_open(list[i], &handle);
			if (ret != 0) {
				handle = NULL;
			}
			break;
		}
	}
	if (count >= 0) {
		libusb_free_device_list(list, 1);
	}
	if (handle) {
		int config = 0;
		libusb_get_configuration(handle, &config);
		if (config != 1) {
			libusb_set_configuration(handle, 1);
		}
		ret = libusb_claim_interface(handle, 0);
		if (ret != 0) {
			libusb_close(handle);
			handle = NULL;
		}
	}
	*err = ret;
	return handle;
}

/* The link to the KIS device at bus and address, claiming it when this
 * process has none. A device another process holds is tried attempts times.
 * No claim or USB request happens under kis_links_mutex. */
static struct kis_link* kis_link_get(uint8_t bus, uint8_t address, int attempts, int enable)
{
	uint32_t location = ((uint32_t)bus << 16) | address;
	libusb_device_handle *handle = NULL;
	struct kis_link *link = NULL;
	int attempt, ret = 0;

	for (attempt = 0; ; attempt++) {
		link = kis_link_find(location);
		if (link) {
			break;
		}
		handle = kis_link_claim(bus, address, &ret);
		if (handle) {
			break;
		}
		if (ret != LIBUSB_ERROR_BUSY || attempt + 1 >= attempts) {
			debug("%s: can't claim the KIS device at %u:%u: %s\n", __func__, bus, address, libusb_error_name(ret));
			return NULL;
		}
		kis_sleep_ms(KIS_OPEN_RETRY_MS);
	}
	if (!link) {
		link = kis_link_new(location);
		if (!link) {
			libusb_release_interface(handle, 0);
			libusb_close(handle);
			return NULL;
		}
		int packet = libusb_get_max_packet_size(libusb_get_device(handle), 0x01);
		if (packet > 0) {
			link->packet = (uint16_t)packet;
		}
		link->handle = handle;
		struct kis_link *other = NULL;
		mutex_lock(&kis_links_mutex);
		FOREACH(struct kis_link *l, &kis_links) {
			if (!l->dead && l->location == location) {
				l->refs++;
				other = l;
				break;
			}
		} ENDFOREACH
		if (!other) {
			collection_add(&kis_links, link);
		}
		mutex_unlock(&kis_links_mutex);
		if (other) {
			kis_link_free(link);
			link = other;
		} else {
			debug("%s: opened the KIS device at %u:%u\n", __func__, bus, address);
		}
	}
	if (enable && kis_link_enable(link) != IRECV_E_SUCCESS) {
		debug("%s: failed to enable the KIS device\n", __func__);
		kis_link_put(link);
		return NULL;
	}
	return link;
}

static uint64_t kis_serial_ecid(const char *serial)
{
	const char *p = strstr(serial, "ECID:");
	return p ? strtoull(p + 5, NULL, 16) : 0;
}

/* A client for what the link shows: a recovery device, opened here, or KIS
 * DFU, which the caller finishes opening as before. With ecid nonzero, a
 * device in DFU is checked against it before anything is opened. Takes the
 * caller's reference to link. */
static irecv_error_t kis_link_open_client(irecv_client_t *pclient, struct kis_link *link, uint64_t ecid)
{
	struct kis_look look;
	irecv_client_t client = NULL;
	unsigned int session = 0;
	char serial[256];
	uint16_t pid = 0;

	*pclient = NULL;
	mutex_lock(&link->xfer);
	irecv_error_t err = kis_link_look(link, &look, USB_TIMEOUT);
	if (err == IRECV_E_SUCCESS && look.state == KIS_STATE_DFU) {
		if (ecid != 0 && kis_serial_ecid(look.serial) != ecid) {
			err = IRECV_E_NO_DEVICE;
		}
	} else if (err == IRECV_E_SUCCESS && look.state == KIS_STATE_RECOVERY) {
		if (kis_link_current(link, &look)) {
			mutex_lock(&link->lock);
			session = link->session;
			mutex_unlock(&link->lock);
		} else {
			err = rsm_attach(link, &look, &session);
		}
		mutex_lock(&link->lock);
		pid = link->pid;
		memcpy(serial, link->serial, sizeof(serial));
		mutex_unlock(&link->lock);
		if (err == IRECV_E_SUCCESS && ecid != 0 && kis_serial_ecid(serial) != ecid) {
			err = IRECV_E_NO_DEVICE;
		}
	} else if (err == IRECV_E_SUCCESS) {
		err = IRECV_E_UNABLE_TO_CONNECT;
	}
	mutex_unlock(&link->xfer);

	if (err == IRECV_E_SUCCESS) {
		client = (irecv_client_t)calloc(1, sizeof(struct irecv_client_private));
		if (!client) {
			err = IRECV_E_OUT_OF_MEMORY;
		}
	}
	if (err != IRECV_E_SUCCESS) {
		kis_link_put(link);
		return err;
	}
	client->kis = link;
	client->handle = link->handle;
	if (look.state == KIS_STATE_DFU) {
		client->mode = KIS_PRODUCT_ID;
	} else {
		client->mode = pid;
		client->rsm_session = session;
		irecv_load_device_info_from_iboot_string(client, serial);
	}
	mutex_lock(&kis_links_mutex);
	link->clients++;
	mutex_unlock(&kis_links_mutex);
	*pclient = client;
	return IRECV_E_SUCCESS;
}
#endif

#ifndef _WIN32
static irecv_error_t irecv_kis_request_init(KIS_req_header *hdr, uint8_t portal, uint16_t index, size_t argCount, size_t payloadSize, size_t rplWords)
{
	if (argCount > UINT8_MAX) {
		return IRECV_E_INVALID_INPUT;
	}

	if (index >= (1 << 10)) {
		return IRECV_E_INVALID_INPUT;
	}

	if (rplWords >= (1 << 14)) {
		return IRECV_E_INVALID_INPUT;
	}

	size_t reqSize = payloadSize + (argCount << 2);
	if (reqSize > UINT32_MAX) {
		return IRECV_E_INVALID_INPUT;
	}

	hdr->sequence         = 0; // Doesn't matter
	hdr->version          = 0xA0;
	hdr->portal           = portal;
	hdr->argCount         = (uint8_t) argCount;
	hdr->indexLo          = (uint8_t) (index & 0xFF);
	hdr->indexHiRplSizeLo = (uint8_t) (((index >> 8) & 0x3) | ((rplWords << 2) & 0xFC));
	hdr->rplSizeHi        = (uint8_t) ((rplWords >> 6) & 0xFF);
	hdr->reqSize          = (uint32_t) reqSize;

	return IRECV_E_SUCCESS;
}

static irecv_error_t irecv_kis_request(irecv_client_t client, KIS_req_header *req, size_t reqSize, KIS_req_header *rpl, size_t *rplSize)
{
#ifdef HAVE_KIS_LINKS
	if (client->kis) {
		return kis_link_raw(client->kis, req, reqSize, rpl, rplSize);
	}
#endif
	int endpoint = 0;
	switch (req->portal) {
		case KIS_PORTAL_CONFIG:
			endpoint = 1;
			break;
		case KIS_PORTAL_RSM:
			endpoint = 3;
			break;
		default:
			debug("Don't know which endpoint to use for portal %d\n", req->portal);
			return IRECV_E_INVALID_INPUT;
	}

	int sent = 0;
	irecv_error_t err = irecv_usb_bulk_transfer(client, endpoint, (unsigned char *) req, reqSize, &sent, USB_TIMEOUT);
	if (err != IRECV_E_SUCCESS) {
		debug("[send] irecv_usb_bulk_transfer failed, error %d\n", err);
		return err;
	}

	if ((size_t) sent != reqSize) {
		debug("sent != reqSize\n");
		return IRECV_E_USB_UPLOAD;
	}

	int rcvd = 0;
	err = irecv_usb_bulk_transfer(client, endpoint | 0x80, (unsigned char *) rpl, *rplSize, &rcvd, USB_TIMEOUT);
	if (err != IRECV_E_SUCCESS) {
		debug("[rcv] irecv_usb_bulk_transfer failed, error %d\n", err);
		return err;
	}

	*rplSize = rcvd;

	return IRECV_E_SUCCESS;
}

static irecv_error_t irecv_kis_config_write32(irecv_client_t client, uint8_t portal, uint16_t index, uint32_t value)
{
	KIS_config_wr32   req  = {};
	KIS_generic_reply rpl = {};
	irecv_error_t err = irecv_kis_request_init(&req.hdr, portal, index, 1, 0, 1);
	if (err != IRECV_E_SUCCESS) {
		debug("Failed to init KIS request, error %d\n", err);
		return err;
	}

	req.value = value;

	size_t rplSize = sizeof(rpl);
	err = irecv_kis_request(client, &req.hdr, sizeof(req), &rpl.hdr, &rplSize);
	if (err != IRECV_E_SUCCESS) {
		debug("Failed to send KIS request, error %d\n", err);
		return err;
	}

	if (rpl.size != 4) {
		debug("Failed to write config, %d bytes written, status %d\n", rpl.size, rpl.status);
		return err;
	}

	return IRECV_E_SUCCESS;
}

static int irecv_kis_read_string(KIS_device_info *di, size_t off, char *buf, size_t buf_size)
{
	off *= 4;

	size_t inputSize = sizeof(KIS_device_info) - sizeof(KIS_req_header);

	if ((off + 2) > inputSize)
		return 0;

	uint8_t len = di->deviceInfo[off];
	uint8_t type = di->deviceInfo[off + 1];

	if (len & 1)
		return 0;

	if (len/2 >= buf_size)
		return 0;

	if ((off + 2 + len) > inputSize)
		return 0;

	if (type != 3)
		return 0;

	buf[len >> 1] = 0;
	for (size_t i = 0; i < len; i += 2) {
		buf[i >> 1] = di->deviceInfo[i + off + 2];
	}

	return len/2;
}
#endif

#ifdef _WIN32
// Device handles are opened with FILE_FLAG_OVERLAPPED, so every ioctl must use an OVERLAPPED
// and must not return before the driver is done with the buffers, even after a timeout.
static BOOL win32_device_io_control(HANDLE handle, DWORD code, void* in_buf, DWORD in_size, void* out_buf, DWORD out_size, DWORD* transferred, DWORD timeout)
{
	OVERLAPPED overlapped;
	DWORD count = 0;
	BOOL ok;

	if (transferred) {
		*transferred = 0;
	}
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!overlapped.hEvent) {
		return FALSE;
	}

	ok = DeviceIoControl(handle, code, in_buf, in_size, out_buf, out_size, NULL, &overlapped);
	if (ok) {
		ok = GetOverlappedResult(handle, &overlapped, &count, FALSE);
	} else if (GetLastError() == ERROR_IO_PENDING) {
		if (WaitForSingleObject(overlapped.hEvent, timeout) != WAIT_OBJECT_0) {
			debug("%s: Device IO control timed out, cancelling IO\n", __func__);
			CancelIo(handle);
		}
		ok = GetOverlappedResult(handle, &overlapped, &count, TRUE);
	}
	CloseHandle(overlapped.hEvent);

	if (transferred) {
		*transferred = count;
	}
	return ok;
}
#endif

static irecv_error_t irecv_kis_init(irecv_client_t client)
{
#ifdef HAVE_KIS_LINKS
	/* The link enabled the device when it opened it. */
	if (client->kis) {
		client->isKIS = 1;
		return IRECV_E_SUCCESS;
	}
#endif
#ifndef _WIN32
	irecv_error_t err = irecv_kis_config_write32(client, KIS_PORTAL_CONFIG, KIS_INDEX_ENABLE_A, KIS_ENABLE_A_VAL);
	if (err != IRECV_E_SUCCESS) {
		debug("Failed to write to KIS_INDEX_ENABLE_A, error %d\n", err);
		return err;
	}

	err = irecv_kis_config_write32(client, KIS_PORTAL_CONFIG, KIS_INDEX_ENABLE_B, KIS_ENABLE_B_VAL);
	if (err != IRECV_E_SUCCESS) {
		debug("Failed to write to KIS_INDEX_ENABLE_B, error %d\n", err);
		return err;
	}
#endif
	client->isKIS = 1;

	return IRECV_E_SUCCESS;
}

static irecv_error_t irecv_kis_load_device_info(irecv_client_t client)
{
	debug("Loading device info in KIS mode...\n");
#ifdef _WIN32
	KIS_device_info kisInfo;
	DWORD transferred = 0;
	int ret = win32_device_io_control(client->handle, 0x220004, NULL, 0, &kisInfo, sizeof(kisInfo), &transferred, USB_TIMEOUT);
	if (ret) {
		debug("Serial: %s\n", kisInfo.serial);
		irecv_load_device_info_from_iboot_string(client, kisInfo.serial);
		debug("Manufacturer: %s\n", kisInfo.manufacturer);
		debug("Product: %s\n", kisInfo.product);
		debug("Nonces: %s\n", kisInfo.nonces);
		irecv_copy_nonce_with_tag_from_buffer("NONC", &client->device_info.ap_nonce, &client->device_info.ap_nonce_size, kisInfo.nonces);
		irecv_copy_nonce_with_tag_from_buffer("SNON", &client->device_info.sep_nonce, &client->device_info.sep_nonce_size, kisInfo.nonces);
		debug("VID: 0x%04x\n", kisInfo.vid);
		debug("PID: 0x%04x\n", kisInfo.pid);
	}
	client->mode = kisInfo.pid;
#else
	KIS_req_header req = {};
	KIS_device_info di = {};
	irecv_error_t err = irecv_kis_request_init(&req, KIS_PORTAL_RSM, KIS_INDEX_GET_INFO, 0, 0, sizeof(di.deviceInfo)/4);
	if (err != IRECV_E_SUCCESS) {
		debug("Failed to init KIS request, error %d\n", err);
		return err;
	}

	size_t rcvSize = sizeof(di);
	err = irecv_kis_request(client, &req, sizeof(req), &di.hdr, &rcvSize);
	if (err != IRECV_E_SUCCESS) {
		debug("Failed to send KIS request, error %d\n", err);
		return err;
	}

	char buf[0x100];
	int len = 0;

	len = irecv_kis_read_string(&di, di.deviceDescriptor.iSerialNumber, buf, sizeof(buf));
	if (len == 0)
		return IRECV_E_INVALID_INPUT;
	debug("Serial: %s\n", buf);

	irecv_load_device_info_from_iboot_string(client, buf);

	len = irecv_kis_read_string(&di, di.deviceDescriptor.iManufacturer, buf, sizeof(buf));
	if (len == 0)
		return IRECV_E_INVALID_INPUT;
	debug("Manufacturer: %s\n", buf);

	len = irecv_kis_read_string(&di, di.deviceDescriptor.iProduct, buf, sizeof(buf));
	if (len == 0)
		return IRECV_E_INVALID_INPUT;
	debug("Product: %s\n", buf);

	len = irecv_kis_read_string(&di, di.nonceOffset, buf, sizeof(buf));
	if (len == 0)
		return IRECV_E_INVALID_INPUT;
	debug("Nonces: %s\n", buf);

	irecv_copy_nonce_with_tag_from_buffer("NONC", &client->device_info.ap_nonce, &client->device_info.ap_nonce_size, buf);
	irecv_copy_nonce_with_tag_from_buffer("SNON", &client->device_info.sep_nonce, &client->device_info.sep_nonce_size, buf);

	debug("VID: 0x%04x\n", di.deviceDescriptor.idVendor);
	debug("PID: 0x%04x\n", di.deviceDescriptor.idProduct);

	client->mode  = di.deviceDescriptor.idProduct;
#endif
	return IRECV_E_SUCCESS;
}

#ifdef _WIN32
static const GUID GUID_DEVINTERFACE_IBOOT = {0xED82A167L, 0xD61A, 0x4AF6, {0x9A, 0xB6, 0x11, 0xE5, 0x22, 0x36, 0xC5, 0x76}};
static const GUID GUID_DEVINTERFACE_DFU = {0xB8085869L, 0xFEB9, 0x404B, {0x8C, 0xB1, 0x1E, 0x5C, 0x14, 0xFA, 0x8C, 0x54}};
static const GUID GUID_DEVINTERFACE_KIS = {0xB36F4137L, 0xF4EF, 0x4BFC, {0xA2, 0x5A, 0xC2, 0x41, 0x07, 0x68, 0xEE, 0x37}};
static const GUID GUID_DEVINTERFACE_PORTDFU = {0xAF633FF1L, 0x1170, 0x4CA6, {0xAE, 0x9E, 0x08, 0xD0, 0x01, 0x42, 0x1E, 0xAA}};

typedef struct usb_control_request {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;

	char data[];
} usb_control_request;

static irecv_error_t win32_open_with_ecid(irecv_client_t* client, uint64_t ecid)
{
	int found = 0;
	const GUID *guids[] = { &GUID_DEVINTERFACE_KIS, &GUID_DEVINTERFACE_PORTDFU, &GUID_DEVINTERFACE_DFU, &GUID_DEVINTERFACE_IBOOT, NULL };
	irecv_client_t _client = (irecv_client_t) malloc(sizeof(struct irecv_client_private));
	if (_client == NULL) {
		return IRECV_E_OUT_OF_MEMORY;
	}
	memset(_client, 0, sizeof(struct irecv_client_private));

	int k;
	for (k = 0; !found && guids[k]; k++) {
		DWORD i;
		SP_DEVICE_INTERFACE_DATA currentInterface;
		HDEVINFO usbDevices = SetupDiGetClassDevs(guids[k], NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (usbDevices == INVALID_HANDLE_VALUE) {
			continue;
		}
		memset(&currentInterface, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
		currentInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
		for (i = 0; SetupDiEnumDeviceInterfaces(usbDevices, NULL, guids[k], i, &currentInterface); i++) {
			_client->handle = INVALID_HANDLE_VALUE;
			DWORD requiredSize = 0;
			PSP_DEVICE_INTERFACE_DETAIL_DATA_A details;
			SetupDiGetDeviceInterfaceDetailA(usbDevices, &currentInterface, NULL, 0, &requiredSize, NULL);
			// device may have vanished between enumeration and detail query
			if (requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A)) {
				continue;
			}
			details = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A) malloc(requiredSize);
			if (!details) {
				continue;
			}
			details->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
			if (!SetupDiGetDeviceInterfaceDetailA(usbDevices, &currentInterface, details, requiredSize, NULL, NULL)) {
				free(details);
				continue;
			}

			unsigned int pid = 0;
			unsigned int vid = 0;
			if (sscanf(details->DevicePath, "\\\\?\\%*3s#vid_%04x&pid_%04x", &vid, &pid) != 2) {
				debug("%s: ERROR: failed to parse VID/PID! path: %s\n", __func__, details->DevicePath);
				free(details);
				continue;
			}
			if (vid != APPLE_VENDOR_ID) {
				free(details);
				continue;
			}

			// make sure the current device is actually in the right mode for the given driver interface
			if ((guids[k] == &GUID_DEVINTERFACE_DFU && pid != IRECV_K_DFU_MODE && pid != IRECV_K_WTF_MODE)
			    || (guids[k] == &GUID_DEVINTERFACE_PORTDFU && pid != IRECV_K_PORT_DFU_MODE)
			    || (guids[k] == &GUID_DEVINTERFACE_IBOOT && (pid < IRECV_K_RECOVERY_MODE_1 || pid > IRECV_K_RECOVERY_MODE_4))
			    || (guids[k] == &GUID_DEVINTERFACE_KIS && pid != 1)
			) {
				free(details);
				continue;
			}
			if (guids[k] == &GUID_DEVINTERFACE_KIS) {
				pid = KIS_PRODUCT_ID;
			}

			_client->handle = CreateFileA(details->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if (_client->handle == INVALID_HANDLE_VALUE) {
				debug("%s: Failed to open device path %s: %d\n", __func__, details->DevicePath, (int)GetLastError());
				free(details);
				continue;
			}
			_client->mode = pid;

			if (ecid == IRECV_K_WTF_MODE) {
				if (_client->mode != IRECV_K_WTF_MODE) {
					/* special ecid case, ignore !IRECV_K_WTF_MODE */
					CloseHandle(_client->handle);
					_client->handle = INVALID_HANDLE_VALUE;
					free(details);
					continue;
				} else {
					ecid = 0;
				}
			}

			if ((ecid != 0) && (_client->mode == IRECV_K_WTF_MODE)) {
				/* we can't get ecid in WTF mode */
				CloseHandle(_client->handle);
				_client->handle = INVALID_HANDLE_VALUE;
				free(details);
				continue;
			}

			char serial_str[256];
			serial_str[0] = '\0';

			if (_client->mode != KIS_PRODUCT_ID) {
				char *p = (char*)details->DevicePath;
				while ((p = strstr(p, "\\usb"))) {
					if (sscanf(p, "\\usb#vid_05ac&pid_%*04x#%s", serial_str) == 1)
						break;
					p += 4;
				}
				free(details);

				if (serial_str[0] == '\0') {
					CloseHandle(_client->handle);
					_client->handle = INVALID_HANDLE_VALUE;
					continue;
				}
				p = strchr(serial_str, '#');
				if (p) {
					*p = '\0';
				}

				unsigned int j;
				for (j = 0; j < strlen(serial_str); j++) {
					if (serial_str[j] == '_') {
						serial_str[j] = ' ';
					} else {
						serial_str[j] = toupper(serial_str[j]);
					}
				}

				irecv_load_device_info_from_iboot_string(_client, serial_str);
			}

			if (ecid != 0 && _client->mode != KIS_PRODUCT_ID) {
				if (_client->device_info.ecid != ecid) {
					CloseHandle(_client->handle);
					_client->handle = INVALID_HANDLE_VALUE;
					free(_client->device_info.srnm);
					free(_client->device_info.imei);
					free(_client->device_info.srtg);
					free(_client->device_info.serial_string);
					memset(&_client->device_info, 0, sizeof(_client->device_info));
					continue;
				}
				debug("found device with ECID %016" PRIx64 "\n", (uint64_t)ecid);
			}
			found = 1;
			break;
		}
		SetupDiDestroyDeviceInfoList(usbDevices);
	}

	if (!found) {
		irecv_close(_client);
		return IRECV_E_UNABLE_TO_CONNECT;
	}

	*client = _client;

	return IRECV_E_SUCCESS;
}
#endif

#ifdef HAVE_IOKIT
static void iokit_cfdictionary_set_short(CFMutableDictionaryRef dict, const void *key, SInt16 value)
{
	CFNumberRef numberRef;

	numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &value);
	if (numberRef) {
		CFDictionarySetValue(dict, key, numberRef);
		CFRelease(numberRef);
	}
}
#endif

static int check_context(irecv_client_t client)
{
	if (client == NULL || client->handle == NULL) {
		return IRECV_E_NO_DEVICE;
	}

	return IRECV_E_SUCCESS;
}
#endif

#ifndef USE_DUMMY
#ifdef HAVE_IOKIT
static int iokit_usb_control_transfer(irecv_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int timeout)
{
	IOReturn result;
	IOUSBDevRequestTO req;

	memset(&req, 0, sizeof(req));
	req.bmRequestType     = bm_request_type;
	req.bRequest          = b_request;
	req.wValue            = OSSwapLittleToHostInt16(w_value);
	req.wIndex            = OSSwapLittleToHostInt16(w_index);
	req.wLength           = OSSwapLittleToHostInt16(w_length);
	req.pData             = data;
	req.noDataTimeout     = timeout;
	req.completionTimeout = timeout;

	result = (*client->handle)->DeviceRequestTO(client->handle, &req);
	switch (result) {
		case kIOReturnSuccess:         return req.wLenDone;
		case kIOUSBPipeStalled:        return IRECV_E_PIPE;
		case kIOReturnTimeout:         return IRECV_E_TIMEOUT;
		case kIOUSBTransactionTimeout: return IRECV_E_TIMEOUT;
		case kIOReturnNotResponding:   return IRECV_E_NO_DEVICE;
		case kIOReturnNoDevice:	       return IRECV_E_NO_DEVICE;
		default:
			return IRECV_E_UNKNOWN_ERROR;
	}
}
#else
#ifdef __APPLE__
	void dummy_callback(void) { }
#endif
#endif
#endif

int irecv_usb_control_transfer(irecv_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int timeout)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
#ifndef _WIN32
#ifdef HAVE_IOKIT
	return iokit_usb_control_transfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, timeout);
#else
	if (client->rsm_session) {
		return rsm_control(client->kis, client->rsm_session, bm_request_type, b_request, w_value, w_index, data, w_length, timeout);
	}
	return libusb_control_transfer(client->handle, bm_request_type, b_request, w_value, w_index, data, w_length, timeout);
#endif
#else
	DWORD count = 0;

	if (data == NULL)
		w_length = 0;

	DWORD packet_size = sizeof(usb_control_request) + w_length;
	usb_control_request* packet = (usb_control_request*) malloc(packet_size);
	if (!packet) {
		return -1;
	}
	packet->bmRequestType = bm_request_type;
	packet->bRequest = b_request;
	packet->wValue = w_value;
	packet->wIndex = w_index;
	packet->wLength = w_length;

	if (bm_request_type < 0x80 && w_length > 0) {
		memcpy(packet->data, data, w_length);
	}

	if (!win32_device_io_control(client->handle, 0x2200A0, packet, packet_size, packet, packet_size, &count, timeout)
	    || count < sizeof(usb_control_request)) {
		free(packet);
		return -1;
	}

	count -= sizeof(usb_control_request);
	if (count > w_length) {
		count = w_length;
	}
	if (count > 0) {
		if (bm_request_type >= 0x80) {
			memcpy(data, packet->data, count);
		}
	}
	free(packet);

	return count;
#endif
#endif
}

#ifndef WIN32
#ifdef HAVE_IOKIT

static void iokit_async_cb(void *refcon, kern_return_t ret, void *arg_0)
{
	struct irecv_async_transfer* transfer = refcon;

	if(transfer != NULL) {
		transfer->ret = ret;
		memcpy(&transfer->len, &arg_0, sizeof(transfer->len));
		CFRunLoopStop(CFRunLoopGetCurrent());
	}
}

static int iokit_async_usb_control_transfer(irecv_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, struct irecv_async_transfer* transfer)
{
	IOReturn result;
	IOUSBDevRequest req;

	memset(&req, 0, sizeof(req));
	req.bmRequestType     = bm_request_type;
	req.bRequest          = b_request;
	req.wValue            = OSSwapLittleToHostInt16(w_value);
	req.wIndex            = OSSwapLittleToHostInt16(w_index);
	req.wLength           = OSSwapLittleToHostInt16(w_length);
	req.pData             = data;
	result = (*client->handle)->DeviceRequestAsync(client->handle, &req, iokit_async_cb, transfer);
	switch (result) {
		case kIOReturnSuccess:         return IRECV_E_SUCCESS;
		case kIOUSBPipeStalled:        return IRECV_E_PIPE;
		case kIOReturnTimeout:         return IRECV_E_TIMEOUT;
		case kIOUSBTransactionTimeout: return IRECV_E_TIMEOUT;
		case kIOReturnNotResponding:   return IRECV_E_NO_DEVICE;
		case kIOReturnNoDevice:	       return IRECV_E_NO_DEVICE;
		default:
			return IRECV_E_UNKNOWN_ERROR;
	}
}

#else
#ifndef USE_DUMMY

static void async_cb(struct libusb_transfer* usb_transfer) {
	struct irecv_async_transfer* transfer = usb_transfer->user_data;
	transfer->ret = usb_transfer->status;
	transfer->len += usb_transfer->actual_length;
	transfer->completed = 1;
}

#endif
#endif
#endif

int irecv_async_usb_control_transfer(irecv_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length) {
#ifdef HAVE_KIS_LINKS
	if (client->kis) {
		return IRECV_E_UNSUPPORTED;
	}
#endif
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
#ifndef _WIN32
#ifdef HAVE_IOKIT
	return iokit_async_usb_control_transfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, NULL);
#else
	irecv_error_t error;

	unsigned char* buffer = malloc(w_length + 8);
	if(!buffer) {
		return IRECV_E_OUT_OF_MEMORY;
	}
	struct libusb_transfer* usb_transfer = libusb_alloc_transfer(0);
	if(!usb_transfer) {
		free(buffer);
		return IRECV_E_OUT_OF_MEMORY;
	}
	memcpy((buffer + 8), data, w_length);
	libusb_fill_control_setup(buffer, bm_request_type, b_request, w_value, w_index, w_length);
	libusb_fill_control_transfer(usb_transfer, client->handle, buffer, NULL, NULL, 0);
	/* nothing waits for this transfer, so libusb frees it and its buffer on completion */
	usb_transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
	error = libusb_submit_transfer(usb_transfer);
	if(error != 0) {
		libusb_free_transfer(usb_transfer);
		return error;
	}
	return IRECV_E_SUCCESS;
#endif
#else
	return IRECV_E_UNSUPPORTED;
#endif
#endif

}

int irecv_async_usb_control_transfer_with_cancel(irecv_client_t client, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, unsigned char *data, uint16_t w_length, unsigned int u_time) {
#ifdef HAVE_KIS_LINKS
	if (client->kis) {
		return IRECV_E_UNSUPPORTED;
	}
#endif
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
#ifndef _WIN32
	irecv_error_t error;
	struct irecv_async_transfer transfer;
	memset(&transfer, 0, sizeof(struct irecv_async_transfer));

#ifdef HAVE_IOKIT

	error = iokit_async_usb_control_transfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, &transfer);
	if(error != IRECV_E_SUCCESS) {
		return error;
	}
	usleep(u_time);
	error = (*client->handle)->USBDeviceAbortPipeZero(client->handle);
	if(error != kIOReturnSuccess) {
		return IRECV_E_UNKNOWN_ERROR;
	}
	while(transfer.ret != kIOReturnAborted){
		CFRunLoopRun();
	}
	return transfer.len;
#else
	unsigned char* buffer = malloc(w_length + 8);
	if(!buffer) {
		return IRECV_E_OUT_OF_MEMORY;
	}
	struct libusb_transfer* usb_transfer = libusb_alloc_transfer(0);
	if(!usb_transfer) {
		free(buffer);
		return IRECV_E_OUT_OF_MEMORY;
	}
	memcpy((buffer + 8), data, w_length);
	libusb_fill_control_setup(buffer, bm_request_type, b_request, w_value, w_index, w_length);
	libusb_fill_control_transfer(usb_transfer, client->handle, buffer, async_cb, &transfer, 0);
	/* libusb frees buffer in libusb_free_transfer() */
	usb_transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
	error = libusb_submit_transfer(usb_transfer);
	if(error != 0) {
		libusb_free_transfer(usb_transfer);
		return error;
	}

	usleep(u_time);

	/* LIBUSB_ERROR_NOT_FOUND only means the transfer already finished;
	   either way the callback has to run before the transfer can be freed */
	libusb_cancel_transfer(usb_transfer);

	while(!transfer.completed) {
		libusb_handle_events_completed(libirecovery_context, &transfer.completed);
	}

	int transferred = (int)transfer.len;
	libusb_free_transfer(usb_transfer);
	return transferred;
#endif
#else
	return IRECV_E_UNSUPPORTED;
#endif
#endif
}

#ifndef USE_DUMMY
#ifdef HAVE_IOKIT
static int iokit_usb_bulk_transfer(irecv_client_t client,
						unsigned char endpoint,
						unsigned char *data,
						int length,
						int *transferred,
						unsigned int timeout)
{
	IOReturn result;
	IOUSBInterfaceInterface300 **intf = client->usbInterface;
	UInt32 size = length;
	UInt8 isUSBIn = (endpoint & kUSBbEndpointDirectionMask) != 0;
	UInt8 numEndpoints;

	if (!intf) return IRECV_E_USB_INTERFACE;

	result = (*intf)->GetNumEndpoints(intf, &numEndpoints);

	if (result != kIOReturnSuccess)
		return IRECV_E_USB_INTERFACE;

	for (UInt8 pipeRef = 0; pipeRef <= numEndpoints; pipeRef++) {
		UInt8 direction = 0;
		UInt8 number = 0;
		UInt8 transferType = 0;
		UInt16 maxPacketSize = 0;
		UInt8 interval = 0;

		result = (*intf)->GetPipeProperties(intf, pipeRef, &direction, &number, &transferType, &maxPacketSize, &interval);
		if (result != kIOReturnSuccess)
			continue;

		if (direction == 3)
			direction = isUSBIn;

		if (number != (endpoint & ~kUSBbEndpointDirectionMask) || direction != isUSBIn)
			continue;

		// Just because
		result = (*intf)->GetPipeStatus(intf, pipeRef);
		switch (result) {
			case kIOReturnSuccess:  break;
			case kIOReturnNoDevice: return IRECV_E_NO_DEVICE;
			case kIOReturnNotOpen:  return IRECV_E_UNABLE_TO_CONNECT;
			default:                return IRECV_E_USB_STATUS;
		}

		// Do the transfer
		if (isUSBIn) {
			result = (*intf)->ReadPipeTO(intf, pipeRef, data, &size, timeout, timeout);
			if (result != kIOReturnSuccess)
				return IRECV_E_PIPE;
			*transferred = size;

			return IRECV_E_SUCCESS;
		}
		else {
			// IOUSBInterfaceClass::interfaceWritePipe (intf?, pipeRef==1, data, size=0x8000)
			result = (*intf)->WritePipeTO(intf, pipeRef, data, size, timeout, timeout);
			if (result != kIOReturnSuccess)
				return IRECV_E_PIPE;
			*transferred = size;

			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_USB_INTERFACE;
}

static int iokit_usb_interrupt_transfer(irecv_client_t client,
						unsigned char endpoint,
						unsigned char *data,
						int length,
						int *transferred,
						unsigned int timeout)
{
	IOReturn result;
	IOUSBInterfaceInterface300 **intf = client->usbInterface;
	UInt32 size = length;
	UInt8 isUSBIn = (endpoint & kUSBbEndpointDirectionMask) != 0;
	UInt8 numEndpoints;

	if (!intf) return IRECV_E_USB_INTERFACE;

	result = (*intf)->GetNumEndpoints(intf, &numEndpoints);

	if (result != kIOReturnSuccess)
		return IRECV_E_USB_INTERFACE;

	for (UInt8 pipeRef = 0; pipeRef <= numEndpoints; pipeRef++) {
		UInt8 direction = 0;
		UInt8 number = 0;
		UInt8 transferType = 0;
		UInt16 maxPacketSize = 0;
		UInt8 interval = 0;

		result = (*intf)->GetPipeProperties(intf, pipeRef, &direction, &number, &transferType, &maxPacketSize, &interval);
		if (result != kIOReturnSuccess)
			continue;

		if (direction == 3)
			direction = isUSBIn;

		if (number != (endpoint & ~kUSBbEndpointDirectionMask) || direction != isUSBIn)
			continue;

		// Just because
		result = (*intf)->GetPipeStatus(intf, pipeRef);
		switch (result) {
			case kIOReturnSuccess:  break;
			case kIOReturnNoDevice: return IRECV_E_NO_DEVICE;
			case kIOReturnNotOpen:  return IRECV_E_UNABLE_TO_CONNECT;
			default:                return IRECV_E_USB_STATUS;
		}

		// Do the transfer
		if (isUSBIn) {
			result = (*intf)->ReadPipeTO(intf, pipeRef, data, &size, timeout, timeout);
			if (result != kIOReturnSuccess)
				return IRECV_E_PIPE;
			*transferred = size;

			return IRECV_E_SUCCESS;
		}
		else {
			result = (*intf)->WritePipeTO(intf, pipeRef, data, size, timeout, timeout);
			if (result != kIOReturnSuccess)
				return IRECV_E_PIPE;
			*transferred = size;

			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_USB_INTERFACE;
}
#endif
#endif

int irecv_usb_bulk_transfer(irecv_client_t client,
							unsigned char endpoint,
							unsigned char *data,
							int length,
							int *transferred,
							unsigned int timeout)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	int ret;

#ifndef _WIN32
#ifdef HAVE_IOKIT
	return iokit_usb_bulk_transfer(client, endpoint, data, length, transferred, timeout);
#else
	if (client->rsm_session) {
		return rsm_bulk(client->kis, client->rsm_session, endpoint, data, length, transferred, timeout);
	}
	ret = libusb_bulk_transfer(client->handle, endpoint, data, length, transferred, timeout);
	if (ret == LIBUSB_ERROR_PIPE) {
		libusb_clear_halt(client->handle, endpoint);
	}
#endif
#else
	if (endpoint==0x4) {
		DWORD count = 0;
		ret = win32_device_io_control(client->handle, 0x2201B6, data, length, data, length, &count, timeout);
		if (transferred) {
			*transferred = (int)count;
		}
	} else {
		ret = 0;
	}
	ret = (ret==0) ? -1 : 0;
#endif

	return ret;
#endif
}

int irecv_usb_interrupt_transfer(irecv_client_t client,
							unsigned char endpoint,
							unsigned char *data,
							int length,
							int *transferred,
							unsigned int timeout)
{
#ifdef HAVE_KIS_LINKS
	if (client->kis) {
		return IRECV_E_UNSUPPORTED;
	}
#endif
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	int ret;

#ifndef _WIN32
#ifdef HAVE_IOKIT
	return iokit_usb_interrupt_transfer(client, endpoint, data, length, transferred, timeout);
#else
	ret = libusb_interrupt_transfer(client->handle, endpoint, data, length, transferred, timeout);
	if (ret < 0) {
		libusb_clear_halt(client->handle, endpoint);
	}
#endif
#else
	// win32
	return IRECV_E_UNSUPPORTED;
#endif

	return ret;
#endif
}

#ifndef USE_DUMMY
#ifdef HAVE_IOKIT
static irecv_error_t iokit_usb_open_service(irecv_client_t *pclient, io_service_t service)
{
	IOReturn result;
	irecv_client_t client;
	SInt32 score;
	UInt16 mode;
	UInt32 locationID;
	IOCFPlugInInterface **plug = NULL;
	CFStringRef serialString;

	client = (irecv_client_t) calloc( 1, sizeof(struct irecv_client_private));

	// Create the plug-in
	result = IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
	if (result != kIOReturnSuccess) {
		IOObjectRelease(service);
		free(client);
		return IRECV_E_UNKNOWN_ERROR;
	}

	// Cache the serial string before discarding the service. The service object
	// has a cached copy, so a request to the hardware device is not required.
	char serial_str[256];
	serial_str[0] = '\0';
	serialString = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, 0);
	if (serialString) {
		CFStringGetCString(serialString, serial_str, sizeof(serial_str), kCFStringEncodingUTF8);
		CFRelease(serialString);
	}
	irecv_load_device_info_from_iboot_string(client, serial_str);

	IOObjectRelease(service);

	// Create the device interface
	result = (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320), (LPVOID *)&(client->handle));
	IODestroyPlugInInterface(plug);
	if (result != kIOReturnSuccess) {
		free(client);
		return IRECV_E_UNKNOWN_ERROR;
	}

	(*client->handle)->GetDeviceProduct(client->handle, &mode);
	(*client->handle)->GetLocationID(client->handle, &locationID);
	client->mode = mode;
	debug("opening device %04x:%04x @ %#010x...\n", kAppleVendorID, client->mode, locationID);

	result = (*client->handle)->USBDeviceOpenSeize(client->handle);
	if (result != kIOReturnSuccess) {
		(*client->handle)->Release(client->handle);
		free(client);
		return IRECV_E_UNABLE_TO_CONNECT;
	}

	*pclient = client;
	return IRECV_E_SUCCESS;
}

static io_iterator_t iokit_usb_get_iterator_for_pid(UInt16 pid)
{
	IOReturn result;
	io_iterator_t iterator;
	CFMutableDictionaryRef matchingDict;

	matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
	iokit_cfdictionary_set_short(matchingDict, CFSTR(kUSBVendorID), kAppleVendorID);
	iokit_cfdictionary_set_short(matchingDict, CFSTR(kUSBProductID), pid);

	result = IOServiceGetMatchingServices(MACH_PORT_NULL, matchingDict, &iterator);
	if (result != kIOReturnSuccess)
		return IO_OBJECT_NULL;

	return iterator;
}

static irecv_error_t iokit_open_with_ecid(irecv_client_t* pclient, uint64_t ecid)
{
	io_service_t service, ret_service;
	io_iterator_t iterator;
	CFStringRef usbSerial = NULL;
	CFStringRef ecidString = NULL;
	CFRange range;

	UInt16 wtf_pids[] = { IRECV_K_WTF_MODE, 0};
	UInt16 all_pids[] = { IRECV_K_WTF_MODE, IRECV_K_DFU_MODE, IRECV_K_PORT_DFU_MODE, IRECV_K_RECOVERY_MODE_1, IRECV_K_RECOVERY_MODE_2, IRECV_K_RECOVERY_MODE_3, IRECV_K_RECOVERY_MODE_4, KIS_PRODUCT_ID, 0 };
	UInt16 *pids = all_pids;
	int i;

	if (pclient == NULL) {
		debug("%s: pclient parameter is null\n", __func__);
		return IRECV_E_INVALID_INPUT;
	}
	if (ecid == IRECV_K_WTF_MODE) {
		/* special ecid case, ignore !IRECV_K_WTF_MODE */
		pids = wtf_pids;
		ecid = 0;
	}
	if (ecid > 0) {
		ecidString = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("%llX"), ecid);
		if (ecidString == NULL) {
			debug("%s: failed to create ECID string\n", __func__);
			return IRECV_E_UNABLE_TO_CONNECT;
		}
	}

	*pclient = NULL;
	ret_service = IO_OBJECT_NULL;

	for (i = 0; (pids[i] > 0 && ret_service == IO_OBJECT_NULL) ; i++) {

		iterator = iokit_usb_get_iterator_for_pid(pids[i]);
		if (iterator) {
			while ((service = IOIteratorNext(iterator))) {

				if (ecid == 0) {
					ret_service = service;
					break;
				}

				if (pids[i] == KIS_PRODUCT_ID) {
					// In KIS Mode, we have to open the device in order to get
					// it's ECID
					irecv_error_t err = iokit_usb_open_service(pclient, service);
					if (err != IRECV_E_SUCCESS) {
						debug("%s: failed to open KIS device\n", __func__);
						continue;
					}

					if (ecidString)
						CFRelease(ecidString);

					return IRECV_E_SUCCESS;
				}

				usbSerial = IORegistryEntryCreateCFProperty(service, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, 0);
				if (usbSerial == NULL) {
					debug("%s: failed to create USB serial string property\n", __func__);
					IOObjectRelease(service);
					continue;
				}

				range = CFStringFind(usbSerial, ecidString, kCFCompareCaseInsensitive);
				if (range.location == kCFNotFound) {
					IOObjectRelease(service);
				} else {
					ret_service = service;
					break;
				}
			}
			if (usbSerial) {
				CFRelease(usbSerial);
				usbSerial = NULL;
			}
			IOObjectRelease(iterator);
		}
	}

	if (ecidString)
		CFRelease(ecidString);

	if (ret_service == IO_OBJECT_NULL)
		return IRECV_E_UNABLE_TO_CONNECT;

	return iokit_usb_open_service(pclient, ret_service);
}
#endif

#ifndef _WIN32
#ifndef HAVE_IOKIT

/* Read the USB serial string the kernel already cached for this device.
 * Linux only; returns the length, or 0 if unavailable. Never fatal. */
static int irecv_linux_sysfs_serial(struct libusb_device_handle *handle, char *out, size_t out_len)
{
#ifdef __linux__
	libusb_device *dev;
	uint8_t bus, addr;
	DIR *d;
	struct dirent *e;
	int got = 0;

	if (!handle || !out || out_len == 0) return 0;
	dev = libusb_get_device(handle);
	if (!dev) return 0;
	bus  = libusb_get_bus_number(dev);
	addr = libusb_get_device_address(dev);

	d = opendir("/sys/bus/usb/devices");
	if (!d) return 0;
	while ((e = readdir(d)) != NULL) {
		char path[512];
		FILE *f;
		int b = -1, a = -1;

		if (e->d_name[0] == '.') continue;

		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", e->d_name);
		f = fopen(path, "r");
		if (!f) continue;
		if (fscanf(f, "%d", &b) != 1) { fclose(f); continue; }
		fclose(f);

		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", e->d_name);
		f = fopen(path, "r");
		if (!f) continue;
		if (fscanf(f, "%d", &a) != 1) { fclose(f); continue; }
		fclose(f);

		if (b != (int)bus || a != (int)addr) continue;

		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/serial", e->d_name);
		f = fopen(path, "r");
		if (!f) break;
		if (fgets(out, (int)out_len, f)) {
			size_t n = strlen(out);
			while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
			got = (int)n;
		}
		fclose(f);
		break;
	}
	closedir(d);
	return got;
#else
	(void)handle; (void)out; (void)out_len;
	return 0;
#endif
}

static irecv_error_t libusb_usb_open_handle_with_descriptor_and_ecid(irecv_client_t *pclient, struct libusb_device_handle *usb_handle, struct libusb_device_descriptor *usb_descriptor, uint64_t ecid)
{
	irecv_client_t client = (irecv_client_t) malloc(sizeof(struct irecv_client_private));
	if (client == NULL) {
		libusb_close(usb_handle);
		return IRECV_E_OUT_OF_MEMORY;
	}

	memset(client, 0, sizeof(struct irecv_client_private));
	client->usb_interface = 0;
	client->handle = usb_handle;
	client->mode = usb_descriptor->idProduct;

	if (client->mode != KIS_PRODUCT_ID) {
		char serial_str[256];
		int slen;
		memset(serial_str, 0, sizeof(serial_str));
		slen = irecv_get_string_descriptor_ascii(client, usb_descriptor->iSerialNumber, (unsigned char*)serial_str, sizeof(serial_str)-1);
		if (slen <= 0 || strstr(serial_str, "CPID:") == NULL) {
			/* Some parts (observed on the Apple T1/iBridge in recovery) do
			 * not answer the string-descriptor control transfer here even
			 * though the kernel enumerated the very same string at attach.
			 * Fall back to the kernel's cached copy rather than proceeding
			 * with an unidentified device. */
			debug("string descriptor read gave %d bytes; trying kernel copy\n", slen);
			memset(serial_str, 0, sizeof(serial_str));
			if (irecv_linux_sysfs_serial(usb_handle, serial_str, sizeof(serial_str)) > 0) {
				debug("recovered serial from kernel\n");
			}
		}
		irecv_load_device_info_from_iboot_string(client, serial_str);
	}

	if (ecid != 0 && client->mode != KIS_PRODUCT_ID) {
		if (client->device_info.ecid != ecid) {
			irecv_close(client);
			return IRECV_E_NO_DEVICE; //wrong device
		}
		debug("found device with ECID %016" PRIx64 "\n", (uint64_t)ecid);
	}

	*pclient = client;
	return IRECV_E_SUCCESS;
}

static irecv_error_t libusb_open_with_ecid(irecv_client_t* pclient, uint64_t ecid)
{
	irecv_error_t ret = IRECV_E_UNABLE_TO_CONNECT;
	int i = 0;
	struct libusb_device* usb_device = NULL;
	struct libusb_device** usb_device_list = NULL;
	struct libusb_device_descriptor usb_descriptor;

	*pclient = NULL;
	int usb_device_count = libusb_get_device_list(libirecovery_context, &usb_device_list);
	for (i = 0; i < usb_device_count; i++) {
		usb_device = usb_device_list[i];
		libusb_get_device_descriptor(usb_device, &usb_descriptor);
		if (usb_descriptor.idVendor == APPLE_VENDOR_ID) {
			/* verify this device is in a mode we understand */
			if (usb_descriptor.idProduct == IRECV_K_RECOVERY_MODE_1 ||
				usb_descriptor.idProduct == IRECV_K_RECOVERY_MODE_2 ||
				usb_descriptor.idProduct == IRECV_K_RECOVERY_MODE_3 ||
				usb_descriptor.idProduct == IRECV_K_RECOVERY_MODE_4 ||
				usb_descriptor.idProduct == IRECV_K_WTF_MODE ||
				usb_descriptor.idProduct == IRECV_K_DFU_MODE ||
				usb_descriptor.idProduct == IRECV_K_PORT_DFU_MODE ||
				usb_descriptor.idProduct == KIS_PRODUCT_ID) {

				if (ecid == IRECV_K_WTF_MODE) {
					if (usb_descriptor.idProduct != IRECV_K_WTF_MODE) {
						/* special ecid case, ignore !IRECV_K_WTF_MODE */
						continue;
					} else {
						ecid = 0;
					}
				}

				if ((ecid != 0) && (usb_descriptor.idProduct == IRECV_K_WTF_MODE)) {
					/* we can't get ecid in WTF mode */
					continue;
				}

				debug("opening device %04x:%04x...\n", usb_descriptor.idVendor, usb_descriptor.idProduct);

				if (usb_descriptor.idProduct == KIS_PRODUCT_ID) {
					struct kis_link *link = kis_link_get(libusb_get_bus_number(usb_device), libusb_get_device_address(usb_device), KIS_OPEN_ATTEMPTS, 1);
					ret = link ? kis_link_open_client(pclient, link, ecid) : IRECV_E_UNABLE_TO_CONNECT;
					if (ret == IRECV_E_SUCCESS) {
						break;
					}
					continue;
				}

				struct libusb_device_handle* usb_handle = NULL;
				int libusb_error = libusb_open(usb_device, &usb_handle);
				if (usb_handle == NULL || libusb_error != 0) {
					debug("%s: can't connect to device: %s\n", __func__, libusb_error_name(libusb_error));

					libusb_close(usb_handle);
					if (ecid != 0) {
						continue;
					}
					libusb_free_device_list(usb_device_list, 1);
					return IRECV_E_UNABLE_TO_CONNECT;
				}

				ret = libusb_usb_open_handle_with_descriptor_and_ecid(pclient, usb_handle, &usb_descriptor, ecid);
				if (ret == IRECV_E_SUCCESS) {
					break;
				}
			}
		}
	}
	libusb_free_device_list(usb_device_list, 1);
	return ret;
}
#endif
#endif
#endif

irecv_error_t irecv_open_with_ecid(irecv_client_t* pclient, uint64_t ecid)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	irecv_error_t error = IRECV_E_UNABLE_TO_CONNECT;

	*pclient = NULL;
	if (libirecovery_debug) {
		irecv_set_debug_level(libirecovery_debug);
	}
#ifndef _WIN32
#ifdef HAVE_IOKIT
	error = iokit_open_with_ecid(pclient, ecid);
#else
	error = libusb_open_with_ecid(pclient, ecid);
#endif
#else
	error = win32_open_with_ecid(pclient, ecid);
#endif
	irecv_client_t client = *pclient;
	if (error != IRECV_E_SUCCESS) {
		irecv_close(client);
		*pclient = NULL;
		return error;
	}

	error = irecv_usb_set_configuration(client, 1);
	if (error != IRECV_E_SUCCESS) {
		debug("Failed to set configuration, error %d\n", error);
		irecv_close(client);
		*pclient = NULL;
		return error;
	}

#ifdef HAVE_IOKIT
	error = (*client->handle)->CreateDeviceAsyncEventSource(client->handle, &client->async_event_source);
	if (error != IRECV_E_SUCCESS) {
		irecv_close(client);
		*pclient = NULL;
		return error;
	}
	CFRunLoopAddSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
#endif

	if (client->mode == IRECV_K_DFU_MODE || client->mode == IRECV_K_PORT_DFU_MODE || client->mode == IRECV_K_WTF_MODE || client->mode == KIS_PRODUCT_ID) {
		error = irecv_usb_set_interface(client, 0, 0);
	} else {
		error = irecv_usb_set_interface(client, 0, 0);
		if (error == IRECV_E_SUCCESS && client->mode > IRECV_K_RECOVERY_MODE_2) {
			error = irecv_usb_set_interface(client, 1, 1);
		}
	}

	if (error != IRECV_E_SUCCESS) {
		debug("Failed to set interface, error %d\n", error);
		irecv_close(client);
		*pclient = NULL;
		return error;
	}

	if (client->mode == KIS_PRODUCT_ID) {
		error = irecv_kis_init(client);
		if (error != IRECV_E_SUCCESS) {
			debug("irecv_kis_init failed, error %d\n", error);
			irecv_close(client);
			*pclient = NULL;
			return error;
		}

		error = irecv_kis_load_device_info(client);
		if (error != IRECV_E_SUCCESS) {
			debug("irecv_kis_load_device_info failed, error %d\n", error);
			irecv_close(client);
			*pclient = NULL;
			return error;
		}
		if (ecid != 0 && client->device_info.ecid != ecid) {
			irecv_close(client);
			*pclient = NULL;
			return IRECV_E_NO_DEVICE; //wrong device
		}
		debug("found device with ECID %016" PRIx64 "\n", (uint64_t)client->device_info.ecid);
	} else {
		irecv_copy_nonce_with_tag(client, "NONC", &client->device_info.ap_nonce, &client->device_info.ap_nonce_size);
		irecv_copy_nonce_with_tag(client, "SNON", &client->device_info.sep_nonce, &client->device_info.sep_nonce_size);
	}

	if (error == IRECV_E_SUCCESS) {
		if ((*pclient)->connected_callback != NULL) {
			irecv_event_t event;
			event.size = 0;
			event.data = NULL;
			event.progress = 0;
			event.type = IRECV_CONNECTED;
			(*pclient)->connected_callback(*pclient, &event);
		}
	}
	return error;
#endif
}

irecv_error_t irecv_usb_set_configuration(irecv_client_t client, int configuration)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

#ifndef _WIN32
	debug("Setting to configuration %d\n", configuration);

#ifdef HAVE_IOKIT
	IOReturn result;

	result = (*client->handle)->SetConfiguration(client->handle, configuration);
	if (result != kIOReturnSuccess) {
		debug("error setting configuration: %#x\n", result);
		return IRECV_E_USB_CONFIGURATION;
	}
#else
	if (client->kis) {
		client->usb_config = configuration;
		return IRECV_E_SUCCESS;
	}
	int current = 0;
	libusb_get_configuration(client->handle, &current);
	if (current != configuration) {
		if (libusb_set_configuration(client->handle, configuration) < 0) {
			return IRECV_E_USB_CONFIGURATION;
		}
	}
#endif
	client->usb_config = configuration;
#endif

	return IRECV_E_SUCCESS;
#endif
}

#ifndef USE_DUMMY
#ifdef HAVE_IOKIT
static IOReturn iokit_usb_get_interface(IOUSBDeviceInterface320 **device, uint8_t ifc, io_service_t *usbInterfacep)
{
	IOUSBFindInterfaceRequest request;
	uint8_t                   current_interface;
	kern_return_t             kresult;
	io_iterator_t             interface_iterator;

	*usbInterfacep = IO_OBJECT_NULL;

	request.bInterfaceClass    = kIOUSBFindInterfaceDontCare;
	request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
	request.bAlternateSetting  = kIOUSBFindInterfaceDontCare;

	kresult = (*device)->CreateInterfaceIterator(device, &request, &interface_iterator);
	if (kresult)
		return kresult;

	for ( current_interface = 0 ; current_interface <= ifc ; current_interface++ ) {
		*usbInterfacep = IOIteratorNext(interface_iterator);
		if (current_interface != ifc)
			(void) IOObjectRelease (*usbInterfacep);
	}
	IOObjectRelease(interface_iterator);

	return kIOReturnSuccess;
}

static irecv_error_t iokit_usb_set_interface(irecv_client_t client, int usb_interface, int usb_alt_interface)
{
	IOReturn result;
	io_service_t interface_service = IO_OBJECT_NULL;
	IOCFPlugInInterface **plugInInterface = NULL;
	SInt32 score;

	// Close current interface
	if (client->usbInterface) {
		result = (*client->usbInterface)->USBInterfaceClose(client->usbInterface);
		result = (*client->usbInterface)->Release(client->usbInterface);
		client->usbInterface = NULL;
	}

	result = iokit_usb_get_interface(client->handle, usb_interface, &interface_service);
	if (result != kIOReturnSuccess) {
		debug("failed to find requested interface: %d\n", usb_interface);
		return IRECV_E_USB_INTERFACE;
	}

	result = IOCreatePlugInInterfaceForService(interface_service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);
	IOObjectRelease(interface_service);
	if (result != kIOReturnSuccess) {
		debug("error creating plug-in interface: %#x\n", result);
		return IRECV_E_USB_INTERFACE;
	}

	result = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID)&client->usbInterface);
	IODestroyPlugInInterface(plugInInterface);
	if (result != kIOReturnSuccess) {
		debug("error creating interface interface: %#x\n", result);
		return IRECV_E_USB_INTERFACE;
	}

	result = (*client->usbInterface)->USBInterfaceOpen(client->usbInterface);
	if (result != kIOReturnSuccess) {
		debug("error opening interface: %#x\n", result);
		return IRECV_E_USB_INTERFACE;
	}

	if (usb_interface == 1) {
		result = (*client->usbInterface)->SetAlternateInterface(client->usbInterface, usb_alt_interface);
		if (result != kIOReturnSuccess) {
			debug("error setting alternate interface: %#x\n", result);
			return IRECV_E_USB_INTERFACE;
		}
	}

	return IRECV_E_SUCCESS;
}
#endif
#endif

irecv_error_t irecv_usb_set_interface(irecv_client_t client, int usb_interface, int usb_alt_interface)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	debug("Setting to interface %d:%d\n", usb_interface, usb_alt_interface);
#ifndef _WIN32
#ifdef HAVE_IOKIT
	if (iokit_usb_set_interface(client, usb_interface, usb_alt_interface) < 0) {
		return IRECV_E_USB_INTERFACE;
	}
#else
	if (client->kis) {
		if (client->rsm_session && usb_interface == 1) {
			if (rsm_control(client->kis, client->rsm_session, 0x01, 0x0B, usb_alt_interface, usb_interface, NULL, 0, USB_TIMEOUT) < 0) {
				return IRECV_E_USB_INTERFACE;
			}
		}
		client->usb_interface = usb_interface;
		client->usb_alt_interface = usb_alt_interface;
		return IRECV_E_SUCCESS;
	}
	if (libusb_claim_interface(client->handle, usb_interface) < 0) {
		return IRECV_E_USB_INTERFACE;
	}

	if (usb_interface == 1) {
		if (libusb_set_interface_alt_setting(client->handle, usb_interface, usb_alt_interface) < 0) {
			return IRECV_E_USB_INTERFACE;
		}
	}
#endif
#else
	if (usb_interface == 1) {
		if (irecv_usb_control_transfer(client, 0, 0x0B, usb_alt_interface, usb_interface, NULL, 0, USB_TIMEOUT) < 0) {
			return IRECV_E_USB_INTERFACE;
		}
	}
#endif
	client->usb_interface = usb_interface;
	client->usb_alt_interface = usb_alt_interface;

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_reset(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

#ifndef _WIN32
#ifdef HAVE_IOKIT
	IOReturn result;

	result = (*client->handle)->ResetDevice(client->handle);
	if (result != kIOReturnSuccess && result != kIOReturnNotResponding) {
		debug("error sending device reset: %#x\n", result);
		return IRECV_E_UNKNOWN_ERROR;
	}

	result = (*client->handle)->USBDeviceReEnumerate(client->handle, 0);
	if (result != kIOReturnSuccess && result != kIOReturnNotResponding) {
		debug("error re-enumerating device: %#x (ignored)\n", result);
	}
#else
	/* Never the Debug USB device. */
	if (!client->kis) {
		libusb_reset_device(client->handle);
	}
#endif
#else
	win32_device_io_control(client->handle, 0x22000C, NULL, 0, NULL, 0, NULL, USB_TIMEOUT);
#endif

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_open_with_ecid_and_attempts(irecv_client_t* pclient, uint64_t ecid, int attempts)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	int i;

	for (i = 0; i < attempts; i++) {
		if (*pclient) {
			irecv_close(*pclient);
			*pclient = NULL;
		}
		if (irecv_open_with_ecid(pclient, ecid) != IRECV_E_SUCCESS) {
			debug("Connection failed. Waiting 1 sec before retry.\n");
			sleep(1);
		} else {
			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_UNABLE_TO_CONNECT;
#endif
}

irecv_error_t irecv_event_subscribe(irecv_client_t client, irecv_event_type type, irecv_event_cb_t callback, void* user_data)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	switch(type) {
	case IRECV_RECEIVED:
		client->received_callback = callback;
		break;

	case IRECV_PROGRESS:
		client->progress_callback = callback;
		break;

	case IRECV_CONNECTED:
		client->connected_callback = callback;
		break;

	case IRECV_PRECOMMAND:
		client->precommand_callback = callback;
		break;

	case IRECV_POSTCOMMAND:
		client->postcommand_callback = callback;
		break;

	case IRECV_DISCONNECTED:
		client->disconnected_callback = callback;
		break;

	default:
		return IRECV_E_UNKNOWN_ERROR;
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_event_unsubscribe(irecv_client_t client, irecv_event_type type)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	switch(type) {
	case IRECV_RECEIVED:
		client->received_callback = NULL;
		break;

	case IRECV_PROGRESS:
		client->progress_callback = NULL;
		break;

	case IRECV_CONNECTED:
		client->connected_callback = NULL;
		break;

	case IRECV_PRECOMMAND:
		client->precommand_callback = NULL;
		break;

	case IRECV_POSTCOMMAND:
		client->postcommand_callback = NULL;
		break;

	case IRECV_DISCONNECTED:
		client->disconnected_callback = NULL;
		break;

	default:
		return IRECV_E_UNKNOWN_ERROR;
	}

	return IRECV_E_SUCCESS;
#endif
}

#ifndef USE_DUMMY
struct irecv_device_event_context {
	irecv_device_event_cb_t callback;
	void *user_data;
};

struct irecv_usb_device_info {
	struct irecv_device_info device_info;
	enum irecv_mode mode;
	uint32_t location;
	int alive;
};

#ifdef _WIN32
struct irecv_win_dev_ctx {
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A details;
	uint32_t location;
};
#else
#ifdef HAVE_IOKIT
struct irecv_iokit_dev_ctx {
	io_service_t device;
	IOUSBDeviceInterface **dev;
};
#endif
#endif

static struct irecv_usb_device_info* _irecv_announce_device(unsigned int mode, int is_kis, const char *serial, uint32_t location)
{
	struct irecv_client_private client_loc;
	memset(&client_loc, 0, sizeof(client_loc));
	client_loc.mode = mode;
	client_loc.isKIS = is_kis;
	irecv_load_device_info_from_iboot_string(&client_loc, serial);

	struct irecv_usb_device_info *usb_dev_info = (struct irecv_usb_device_info*)malloc(sizeof(struct irecv_usb_device_info));
	memcpy(&(usb_dev_info->device_info), &(client_loc.device_info), sizeof(struct irecv_device_info));
	usb_dev_info->location = location;
	usb_dev_info->alive = 1;
	usb_dev_info->mode = client_loc.mode;

	mutex_lock(&device_mutex);
	collection_add(&devices, usb_dev_info);
	mutex_unlock(&device_mutex);

	irecv_device_event_t dev_event;
	dev_event.type = IRECV_DEVICE_ADD;
	dev_event.mode = client_loc.mode;
	dev_event.device_info = &(usb_dev_info->device_info);

	mutex_lock(&listener_mutex);
	FOREACH(struct irecv_device_event_context* context, &listeners) {
		context->callback(&dev_event, context->user_data);
	} ENDFOREACH
	mutex_unlock(&listener_mutex);

	return usb_dev_info;
}

static int _irecv_is_recovery_device(void *device)
{
	uint16_t vendor_id = 0;
	uint16_t product_id = 0;
#ifdef _WIN32
	const char *path = (const char*)device;
	unsigned int vendor = 0;
	unsigned int product = 0;
	if (sscanf(path, "\\usb#vid_%04x&pid_%04x#", &vendor, &product) != 2) {
		return 0;
	}
	vendor_id = (uint16_t)vendor;
	product_id = (uint16_t)product;
#else
#ifdef HAVE_IOKIT
	kern_return_t kr;
	IOUSBDeviceInterface **dev = device;
	kr = (*dev)->GetDeviceVendor(dev, &vendor_id);
	if (kr != kIOReturnSuccess) {
		debug("%s: Failed to get vendor id\n", __func__);
		return 0;
	}
	kr = (*dev)->GetDeviceProduct(dev, &product_id);
	if (kr != kIOReturnSuccess) {
		debug("%s: Failed to get product id\n", __func__);
		return 0;
	}
#else
	libusb_device *device_ = (libusb_device*)device;
	struct libusb_device_descriptor devdesc;
	int libusb_error;

	libusb_error = libusb_get_device_descriptor(device_, &devdesc);
	if (libusb_error != 0) {
		debug("%s: failed to get device descriptor: %s\n", __func__, libusb_error_name(libusb_error));
		return 0;
	}
	vendor_id = devdesc.idVendor;
	product_id = devdesc.idProduct;
#endif
#endif

	if (vendor_id != APPLE_VENDOR_ID) {
		return 0;
	}

	switch (product_id) {
		case IRECV_K_DFU_MODE:
		case IRECV_K_WTF_MODE:
		case IRECV_K_RECOVERY_MODE_1:
		case IRECV_K_RECOVERY_MODE_2:
		case IRECV_K_RECOVERY_MODE_3:
		case IRECV_K_RECOVERY_MODE_4:
		case IRECV_K_PORT_DFU_MODE:
		case KIS_PRODUCT_ID:
			break;
		default:
			return 0;
	}
	return 1;
}

static void* _irecv_handle_device_add(void *userdata)
{
	struct irecv_client_private client_loc;
	char serial_str[256];
	uint32_t location = 0;
	uint16_t product_id = 0;
	irecv_error_t error = 0;
	irecv_client_t client = NULL;

	memset(serial_str, 0, sizeof(serial_str));
#ifdef _WIN32
	struct irecv_win_dev_ctx *win_ctx = (struct irecv_win_dev_ctx*)userdata;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A details = win_ctx->details;
	LPSTR result = (LPSTR)details->DevicePath;
	location = win_ctx->location;

	unsigned int pid = 0;

	if (strncmp(result, "\\\\?\\kis#", 8) == 0) {
		pid = KIS_PRODUCT_ID;
	} else {
		char *p = result;
		while ((p = strstr(p, "\\usb"))) {
			if (sscanf(p, "\\usb#vid_05ac&pid_%04x#%s", &pid, serial_str) == 2)
				break;
			p += 4;
		}

		if (serial_str[0] == '\0') {
			debug("%s: ERROR: failed to parse DevicePath?!\n", __func__);
			return NULL;
		}

		if (!_irecv_is_recovery_device(p)) {
			return NULL;
		}
	}

	product_id = (uint16_t)pid;

	if (product_id == KIS_PRODUCT_ID) {
		client = (irecv_client_t)malloc(sizeof(struct irecv_client_private));
		if (client == NULL) {
			debug("%s: Failed to allocate memory\n", __func__);
			return NULL;
		}
		memset(client, 0, sizeof(struct irecv_client_private));
		client->handle = CreateFileA(result, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (client->handle == INVALID_HANDLE_VALUE) {
			debug("%s: Failed to open device path %s\n", __func__, result);
			free(client);
			return NULL;
		}
		client->mode = pid;
	} else {
		char* p = strchr(serial_str, '#');
		if (p) {
			*p = '\0';
		}

		unsigned int j;
		for (j = 0; j < strlen(serial_str); j++) {
			if (serial_str[j] == '_') {
				serial_str[j] = ' ';
			} else {
				serial_str[j] = toupper(serial_str[j]);
			}
		}
	}

#else /* !_WIN32 */
#ifdef HAVE_IOKIT
	struct irecv_iokit_dev_ctx* iokit_ctx = (struct irecv_iokit_dev_ctx*)userdata;
	io_service_t device = iokit_ctx->device;
	IOUSBDeviceInterface **dev = iokit_ctx->dev;

	if (!device) {
		debug("%s: ERROR: no device?!\n", __func__);
		return NULL;
	}
	if (!dev) {
		debug("%s: ERROR: no device interface?!\n", __func__);
		return NULL;
	}

	(*dev)->GetDeviceProduct(dev, &product_id);
	if (!product_id) {
		debug("%s: ERROR: could not get product id?!\n", __func__);
		return NULL;
	}
	CFNumberRef locationNum = (CFNumberRef)IORegistryEntryCreateCFProperty(device, CFSTR(kUSBDevicePropertyLocationID), kCFAllocatorDefault, 0);
	if (locationNum) {
		CFNumberGetValue(locationNum, kCFNumberSInt32Type, &location);
		CFRelease(locationNum);
	}
	if (!location) {
		debug("%s: ERROR: could not get locationID?!\n", __func__);
		return NULL;
	}

	if (product_id == KIS_PRODUCT_ID) {
		int i = 0;
		for (i = 0; i < 10; i++) {
			IOObjectRetain(device);
			error = iokit_usb_open_service(&client, device);
			if (error == IRECV_E_SUCCESS) {
				break;
			}
			debug("%s: Could not open KIS device, retrying...\n", __func__);
			usleep(500000);
		}
		if (error != IRECV_E_SUCCESS) {
			debug("%s: ERROR: could not open KIS device!\n", __func__);
			return NULL;
		}
		product_id = client->mode;
	} else {
		CFStringRef serialString = (CFStringRef)IORegistryEntryCreateCFProperty(device, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, 0);
		if (serialString) {
			CFStringGetCString(serialString, serial_str, sizeof(serial_str), kCFStringEncodingUTF8);
			CFRelease(serialString);
		}
	}
#else /* !HAVE_IOKIT */
	libusb_device *device = (libusb_device*)userdata;
	struct libusb_device_descriptor devdesc;
	struct libusb_device_handle* usb_handle = NULL;
	int libusb_error;

	libusb_error = libusb_get_device_descriptor(device, &devdesc);
	if (libusb_error != 0) {
		debug("%s: ERROR: failed to get device descriptor: %s\n", __func__, libusb_error_name(libusb_error));
		return NULL;
	}
	product_id = devdesc.idProduct;

	uint8_t bus = libusb_get_bus_number(device);
	uint8_t address = libusb_get_device_address(device);
	location = (bus << 16) | address;

	libusb_error = libusb_open(device, &usb_handle);
	if (usb_handle == NULL || libusb_error != 0) {
		debug("%s: ERROR: can't connect to device: %s\n", __func__, libusb_error_name(libusb_error));
		libusb_close(usb_handle);
		return 0;
	}

	if (product_id == KIS_PRODUCT_ID) {
		error = libusb_usb_open_handle_with_descriptor_and_ecid(&client, usb_handle, &devdesc, 0);
		if (error != IRECV_E_SUCCESS) {
			debug("%s: ERROR: could not open KIS device!\n", __func__);
			return NULL;
		}

		product_id = client->mode;
	} else {
		memset(serial_str, 0, sizeof(serial_str));
		libusb_error = libusb_get_string_descriptor_ascii(usb_handle, devdesc.iSerialNumber, (unsigned char*)serial_str, sizeof(serial_str)-1);
		if (libusb_error < 0 || strstr(serial_str, "CPID:") == NULL) {
			/* The Apple T1/iBridge in recovery times out on this control
			 * transfer (LIBUSB_ERROR_TIMEOUT). Bailing out here means no
			 * IRECV_DEVICE_ADD is ever emitted and callers conclude that no
			 * device is attached. The kernel already enumerated the same
			 * string at attach, so fall back to its copy. */
			debug("%s: string descriptor read failed (%s); trying kernel copy\n",
			      __func__, libusb_error_name(libusb_error));
			memset(serial_str, 0, sizeof(serial_str));
			if (irecv_linux_sysfs_serial(usb_handle, serial_str, sizeof(serial_str)) <= 0) {
				debug("%s: no kernel copy either; giving up on this device\n", __func__);
				libusb_close(usb_handle);
				return 0;
			}
			debug("%s: recovered serial from kernel\n", __func__);
		}
		libusb_close(usb_handle);
	}
#endif /* !HAVE_IOKIT */
#endif /* !_WIN32 */
	memset(&client_loc, 0, sizeof(client_loc));
	if (product_id == KIS_PRODUCT_ID) {
		int i = 0;
		for (i = 0; i < 10; i++) {
			error = irecv_usb_set_configuration(client, 1);
			if (error == IRECV_E_SUCCESS) {
				break;
			}
			debug("Failed to set configuration, error %d, retrying...\n", error);
			usleep(500000);
		}
		if (error != IRECV_E_SUCCESS) {
			debug("Failed to set configuration, error %d\n", error);
			irecv_close(client);
			return NULL;
		}

		for (i = 0; i < 10; i++) {
			error = irecv_usb_set_interface(client, 0, 0);
			if (error == IRECV_E_SUCCESS) {
				break;
			}
			debug("Failed to set interface, error %d, retrying...\n", error);
			usleep(500000);
		}
		if (error != IRECV_E_SUCCESS) {
			debug("Failed to set interface, error %d\n", error);
			irecv_close(client);
			return NULL;
		}

		error = irecv_kis_init(client);
		if (error != IRECV_E_SUCCESS) {
			debug("irecv_kis_init failed, error %d\n", error);
			irecv_close(client);
			return NULL;
		}

		error = irecv_kis_load_device_info(client);
		if (error != IRECV_E_SUCCESS) {
			debug("irecv_kis_load_device_info failed, error %d\n", error);
			irecv_close(client);
			return NULL;
		}
		debug("found device with ECID %016" PRIx64 "\n", (uint64_t)client->device_info.ecid);
		strncpy(serial_str, client->device_info.serial_string, 255);
		product_id = client->mode;
		client_loc.isKIS = 1;
	}
	if (client) {
		irecv_close(client);
	}

	_irecv_announce_device(product_id, client_loc.isKIS, serial_str, location);

	return NULL;
}

// Caller must not hold device_mutex; listeners are notified without it.
static void _irecv_handle_device_remove(struct irecv_usb_device_info *devinfo)
{
	irecv_device_event_t dev_event;
	mutex_lock(&device_mutex);
	collection_remove(&devices, devinfo);
	mutex_unlock(&device_mutex);
	dev_event.type = IRECV_DEVICE_REMOVE;
	dev_event.mode = devinfo->mode;
	dev_event.device_info = &(devinfo->device_info);
	mutex_lock(&listener_mutex);
	FOREACH(struct irecv_device_event_context* context, &listeners) {
		context->callback(&dev_event, context->user_data);
	} ENDFOREACH
	mutex_unlock(&listener_mutex);
	free(devinfo->device_info.srnm);
	devinfo->device_info.srnm = NULL;
	free(devinfo->device_info.imei);
	devinfo->device_info.imei = NULL;
	free(devinfo->device_info.srtg);
	devinfo->device_info.srtg = NULL;
	free(devinfo->device_info.serial_string);
	devinfo->device_info.serial_string = NULL;
	devinfo->alive = 0;
	free(devinfo);
}

#ifdef HAVE_KIS_LINKS
enum kis_hold {
	KIS_HOLD_NONE,
	KIS_HOLD_LINGER,  /* after a client that could end the stage closed */
	KIS_HOLD_HANDOFF, /* the stage ended: until the next one is opened */
};

/* A KIS device the event thread watches. Touched only by the event thread. */
struct kis_watch {
	uint32_t location;
	uint8_t bus;
	uint8_t address;
	unsigned int polls;
	int enabled;
	/* A link kept across a stage change, and until when. */
	struct kis_link *held;
	enum kis_hold hold;
	uint64_t deadline;
	unsigned int sessions;
	/* What the device showed last. */
	enum kis_state state;
	unsigned int chan;
	uint32_t host;
	uint32_t dev;
	/* The identity read for this device since it arrived, and the recovery
	 * product id a client of this process read through it. */
	int known;
	char serial[256];
	uint16_t pid;
	/* The device announced for it. */
	enum kis_state view;
	struct irecv_usb_device_info *announced;
};

static struct collection kis_watches;

static struct kis_watch* kis_watch_find(uint32_t location)
{
	FOREACH(struct kis_watch *watch, &kis_watches) {
		if (watch->location == location) {
			return watch;
		}
	} ENDFOREACH
	return NULL;
}

/* Whether link goes to the watched device. Caller holds kis_links_mutex. */
static int kis_watch_has(const struct kis_watch *watch, const struct kis_link *link)
{
	return link->location == watch->location;
}

/* This process's link to the watched device, with a reference. */
static struct kis_link* kis_watch_link(const struct kis_watch *watch)
{
	struct kis_link *link = NULL;

	mutex_lock(&kis_links_mutex);
	FOREACH(struct kis_link *l, &kis_links) {
		if (!l->dead && kis_watch_has(watch, l)) {
			l->refs++;
			link = l;
			break;
		}
	} ENDFOREACH
	mutex_unlock(&kis_links_mutex);
	return link;
}

/* A link a client left for the event thread, with that reference. */
static struct kis_link* kis_watch_take_retained(const struct kis_watch *watch)
{
	struct kis_link *link = NULL;

	mutex_lock(&kis_links_mutex);
	FOREACH(struct kis_link *l, &kis_links) {
		if (kis_watch_has(watch, l) && l->retained == 1) {
			l->retained = 2;
			link = l;
			break;
		}
	} ENDFOREACH
	mutex_unlock(&kis_links_mutex);
	return link;
}

static void kis_link_release_retained(struct kis_link *link)
{
	mutex_lock(&kis_links_mutex);
	link->retained = 0;
	mutex_unlock(&kis_links_mutex);
	kis_link_put(link);
}

static void kis_watch_release(struct kis_watch *watch)
{
	if (watch->held) {
		debug("%s: %08x released\n", __func__, watch->location);
		kis_link_release_retained(watch->held);
		watch->held = NULL;
	}
	watch->hold = KIS_HOLD_NONE;
}

static void kis_watch_remove(uint32_t location)
{
	struct kis_watch *watch = kis_watch_find(location);
	struct kis_link *left;

	if (!watch) {
		return;
	}
	collection_remove(&kis_watches, watch);
	if (watch->announced) {
		_irecv_handle_device_remove(watch->announced);
	}
	kis_watch_release(watch);
	left = kis_watch_take_retained(watch);
	if (left) {
		kis_link_release_retained(left);
	}
	free(watch);
}

/* Keep, or let go of, the link held across a stage change. ended: the stage
 * the device was in has ended; look: what it shows now. */
static void kis_watch_hold(struct kis_watch *watch, const struct kis_look *look, int ended, int clients, unsigned int sessions)
{
	uint64_t now = kis_now_ms();

	if (watch->hold == KIS_HOLD_LINGER) {
		if (clients > 0) {
			watch->deadline = now + KIS_LINGER_MS;
		}
		if (ended) {
			debug("%s: %08x stage ended\n", __func__, watch->location);
			watch->hold = KIS_HOLD_HANDOFF;
			watch->deadline = now + KIS_HANDOFF_MS;
			watch->sessions = sessions;
		}
	} else if (watch->hold == KIS_HOLD_HANDOFF) {
		/* Done once the device is back in DFU or out of recovery, or a client
		 * here has opened the next stage. */
		if (look->state == KIS_STATE_DFU || look->state == KIS_STATE_OTHER || (clients > 0 && sessions != watch->sessions)) {
			watch->deadline = now;
		}
	}
	if (now >= watch->deadline) {
		kis_watch_release(watch);
	}
}

/* Turn what each watched device shows into device events, one per poll, so a
 * listener sees a removal before the device that replaces it. */
static void kis_watch_poll(void)
{
	FOREACH(struct kis_watch *watch, &kis_watches) {
		struct kis_link *link;
		struct kis_look look;
		int borrowed = 0, clients = 0;
		unsigned int sessions = 0;

		if (!watch->held) {
			watch->held = kis_watch_take_retained(watch);
			if (watch->held) {
				debug("%s: %08x kept after its client\n", __func__, watch->location);
				watch->hold = KIS_HOLD_LINGER;
				watch->deadline = kis_now_ms() + KIS_LINGER_MS;
			}
		}
		link = watch->held;
		if (!link) {
			link = kis_watch_link(watch);
			if (!link) {
				/* No client here: look now and then, and pass over the device
				 * while another process has it. */
				if ((watch->polls++ % KIS_LOOK_POLLS) != 0) {
					continue;
				}
				link = kis_link_get(watch->bus, watch->address, 1, 0);
				if (!link) {
					continue;
				}
			}
			borrowed = 1;
		}
		kis_link_look(link, &look, KIS_LOOK_TIMEOUT);
		if ((look.state == KIS_STATE_EMPTY || look.state == KIS_STATE_OTHER) && !watch->enabled) {
			/* Once per arrival: the doorbells may not read back until the device
			 * has been enabled, which nobody may have done yet. */
			watch->enabled = 1;
			if (kis_link_enable(link) == IRECV_E_SUCCESS) {
				kis_link_look(link, &look, KIS_LOOK_TIMEOUT);
			}
		}
		if (look.state == KIS_STATE_RECOVERY && kis_link_current(link, &look)) {
			/* A client here has this stage open: its identity is the device's. */
			mutex_lock(&link->lock);
			snprintf(watch->serial, sizeof(watch->serial), "%s", link->serial);
			watch->pid = link->pid;
			watch->known = 1;
			mutex_unlock(&link->lock);
		}
		mutex_lock(&link->lock);
		sessions = link->sessions;
		mutex_unlock(&link->lock);
		mutex_lock(&kis_links_mutex);
		clients = link->clients;
		mutex_unlock(&kis_links_mutex);
		if (borrowed) {
			kis_link_put(link);
		}
		if (look.state == KIS_STATE_FAILED) {
			if (watch->held) {
				kis_watch_hold(watch, &look, 0, clients, sessions);
			}
			continue;
		}

		/* A stage ends when the device leaves it, or when its channel is
		 * reinitialised: both offsets back to 0. */
		int ended = 0;
		if (watch->state == KIS_STATE_DFU && look.state != KIS_STATE_DFU) {
			ended = 1;
		} else if (watch->state == KIS_STATE_RECOVERY && (look.state != KIS_STATE_RECOVERY || look.chan != watch->chan || (look.host == 0 && look.dev == 0 && (watch->host != 0 || watch->dev != 0)))) {
			ended = 1;
		}
		if (look.state != watch->state || ended) {
			debug("%s: %08x shows %s\n", __func__, watch->location, look.state == KIS_STATE_DFU ? "DFU" : look.state == KIS_STATE_RECOVERY ? "recovery" : look.state == KIS_STATE_EMPTY ? "nothing" : "no DFU or recovery");
		}
		watch->state = look.state;
		watch->chan = look.chan;
		watch->host = look.host;
		watch->dev = look.dev;
		if (look.state == KIS_STATE_DFU) {
			snprintf(watch->serial, sizeof(watch->serial), "%s", look.serial);
			watch->known = 1;
		}
		if (watch->held) {
			kis_watch_hold(watch, &look, ended, clients, sessions);
		}

		enum kis_state view = KIS_STATE_EMPTY;
		uint16_t mode = 0;
		if (look.state == KIS_STATE_DFU) {
			view = KIS_STATE_DFU;
			mode = look.mode;
		} else if (look.state == KIS_STATE_RECOVERY && watch->known) {
			/* The product id is the one read through a client here, if any. */
			view = KIS_STATE_RECOVERY;
			mode = watch->pid ? watch->pid : IRECV_K_RECOVERY_MODE_2;
		}
		if (watch->announced && (view != watch->view || ended)) {
			struct irecv_usb_device_info *gone = watch->announced;
			watch->announced = NULL;
			_irecv_handle_device_remove(gone);
			continue;
		}
		if (!watch->announced && view != KIS_STATE_EMPTY) {
			debug("%s: %s %04x %s\n", __func__, view == KIS_STATE_DFU ? "DFU" : "recovery", mode, view == KIS_STATE_DFU ? look.serial : watch->serial);
			watch->view = view;
			watch->announced = _irecv_announce_device(mode, view == KIS_STATE_DFU, view == KIS_STATE_DFU ? look.serial : watch->serial, watch->location);
		}
	} ENDFOREACH
}

/* The event thread is ending: let go of every link kept for it. */
static void kis_watch_clear(void)
{
	struct collection kept;

	FOREACH(struct kis_watch *watch, &kis_watches) {
		collection_remove(&kis_watches, watch);
		/* The event subscription frees announced devices with the rest. */
		kis_watch_release(watch);
		free(watch);
	} ENDFOREACH
	collection_init(&kept);
	mutex_lock(&kis_links_mutex);
	kis_links_retained = 0;
	FOREACH(struct kis_link *link, &kis_links) {
		if (link->retained == 1) {
			collection_add(&kept, link);
		}
	} ENDFOREACH
	mutex_unlock(&kis_links_mutex);
	FOREACH(struct kis_link *link, &kept) {
		kis_link_release_retained(link);
	} ENDFOREACH
	collection_free(&kept);
}

static void kis_watch_start(void)
{
	collection_init(&kis_watches);
	mutex_lock(&kis_links_mutex);
	kis_links_retained = 1;
	mutex_unlock(&kis_links_mutex);
}

static void kis_watch_add(libusb_device *device)
{
	uint8_t bus = libusb_get_bus_number(device);
	uint8_t address = libusb_get_device_address(device);
	uint32_t location = ((uint32_t)bus << 16) | address;
	if (kis_watch_find(location)) {
		return;
	}
	struct kis_watch *watch = calloc(1, sizeof(struct kis_watch));
	if (!watch) {
		return;
	}
	watch->location = location;
	watch->bus = bus;
	watch->address = address;
	watch->state = KIS_STATE_FAILED;
	watch->view = KIS_STATE_EMPTY;
	collection_add(&kis_watches, watch);
}
#endif


#if defined(_WIN32) || (!defined(HAVE_IOKIT) && !defined(HAVE_LIBUSB_HOTPLUG_API))
static void _irecv_remove_dead_devices(void)
{
	struct collection dead;
	collection_init(&dead);
	mutex_lock(&device_mutex);
	FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
		if (!devinfo->alive) {
			collection_add(&dead, devinfo);
		}
	} ENDFOREACH
	mutex_unlock(&device_mutex);

	FOREACH(struct irecv_usb_device_info *devinfo, &dead) {
		debug("%s: removed ecid: %016" PRIx64 ", location: %d\n",__func__, (uint64_t)devinfo->device_info.ecid, devinfo->location);
		_irecv_handle_device_remove(devinfo);
	} ENDFOREACH
	collection_free(&dead);
}
#endif

#if !defined(_WIN32) && (defined(HAVE_IOKIT) || defined(HAVE_LIBUSB_HOTPLUG_API))
static struct irecv_usb_device_info* _irecv_find_device_by_location(uint32_t location)
{
	struct irecv_usb_device_info *found = NULL;
	mutex_lock(&device_mutex);
	FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
		if (devinfo->location == location) {
			found = devinfo;
			break;
		}
	} ENDFOREACH
	mutex_unlock(&device_mutex);
	return found;
}
#endif

#ifndef _WIN32
#ifdef HAVE_IOKIT
static void iokit_device_added(void *refcon, io_iterator_t iterator)
{
	kern_return_t kr;
	io_service_t device;
	IOCFPlugInInterface **plugInInterface = NULL;
	IOUSBDeviceInterface **dev = NULL;
	HRESULT result;
	SInt32 score;

	while ((device = IOIteratorNext(iterator))) {
		kr = IOCreatePlugInInterfaceForService(device, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);
		if ((kIOReturnSuccess != kr) || !plugInInterface) {
			debug("%s: ERROR: Unable to create a plug-in (%08x)\n", __func__, kr);
			IOObjectRelease(device);
			continue;
		}
		result = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320), (LPVOID *)&dev);
		(*plugInInterface)->Release(plugInInterface);

		if (result || !dev) {
			debug("%s: ERROR: Couldn't create a device interface (%08x)\n", __func__, (int)result);
			IOObjectRelease(device);
			continue;
		}

		if (!_irecv_is_recovery_device(dev)) {
			(void) (*dev)->Release(dev);
			IOObjectRelease(device);
			continue;
		}

		struct irecv_iokit_dev_ctx idev;
		idev.device = device;
		idev.dev = dev;
		_irecv_handle_device_add(&idev);
		(void) (*dev)->Release(dev);
		IOObjectRelease(device);
	}
}

static void iokit_device_removed(void *refcon, io_iterator_t iterator)
{
	io_service_t device;

	while ((device = IOIteratorNext(iterator))) {
		uint32_t location = 0;
		CFNumberRef locationNum = (CFNumberRef)IORegistryEntryCreateCFProperty(device, CFSTR(kUSBDevicePropertyLocationID), kCFAllocatorDefault, 0);
		if (locationNum) {
			CFNumberGetValue(locationNum, kCFNumberSInt32Type, &location);
			CFRelease(locationNum);
		}
		IOObjectRelease(device);

		if (!location) {
			continue;
		}

		struct irecv_usb_device_info *devinfo = _irecv_find_device_by_location(location);
		if (devinfo) {
			_irecv_handle_device_remove(devinfo);
		}
	}
}
#else /* !HAVE_IOKIT */
#ifdef HAVE_LIBUSB_HOTPLUG_API
static int _irecv_usb_hotplug_cb(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data)
{
	if (!_irecv_is_recovery_device(device)) {
		return 0;
	}
	struct libusb_device_descriptor devdesc;
	if (libusb_get_device_descriptor(device, &devdesc) == 0 && devdesc.idProduct == KIS_PRODUCT_ID) {
		if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
			kis_watch_add(device);
		} else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
			kis_watch_remove(((uint32_t)libusb_get_bus_number(device) << 16) | libusb_get_device_address(device));
		}
		return 0;
	}
	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
		THREAD_T th_device;
		if (thread_new(&th_device, _irecv_handle_device_add, device) != 0) {
			debug("%s: FATAL: failed to create thread to handle device add\n", __func__);
			return 0;
		}
		thread_detach(th_device);
	} else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
		uint8_t bus = libusb_get_bus_number(device);
		uint8_t address = libusb_get_device_address(device);
		uint32_t location = (bus << 16) | address;
		struct irecv_usb_device_info *devinfo = _irecv_find_device_by_location(location);
		if (devinfo) {
			_irecv_handle_device_remove(devinfo);
		}
	}

	return 0;
}
#endif /* HAVE_LIBUSB_HOTPLUG_API */
#endif /* !HAVE_IOKIT */
#endif /* !_WIN32 */

struct _irecv_event_handler_info {
	cond_t startup_cond;
	mutex_t startup_mutex;
};

static void *_irecv_event_handler(void* data)
{
	struct _irecv_event_handler_info* info = (struct _irecv_event_handler_info*)data;
#ifdef _WIN32
	struct collection newDevices;
	const GUID *guids[] = { &GUID_DEVINTERFACE_KIS, &GUID_DEVINTERFACE_PORTDFU, &GUID_DEVINTERFACE_DFU, &GUID_DEVINTERFACE_IBOOT, NULL };
	int running = 1;

	collection_init(&newDevices);

	mutex_lock(&(info->startup_mutex));
	cond_signal(&(info->startup_cond));
	mutex_unlock(&(info->startup_mutex));

	do {
		SP_DEVICE_INTERFACE_DATA currentInterface;
		HDEVINFO usbDevices;
		DWORD i;
		int k;

		mutex_lock(&device_mutex);
		FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
			devinfo->alive = 0;
		} ENDFOREACH
		mutex_unlock(&device_mutex);

		for (k = 0; guids[k]; k++) {
			usbDevices = SetupDiGetClassDevs(guids[k], NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
			if (usbDevices == INVALID_HANDLE_VALUE) {
				debug("%s: ERROR: SetupDiGetClassDevs failed\n", __func__);
				FOREACH(struct irecv_win_dev_ctx *win_ctx, &newDevices) {
					free(win_ctx->details);
					collection_remove(&newDevices, win_ctx);
					free(win_ctx);
				} ENDFOREACH
				collection_free(&newDevices);
				return NULL;
			}

			memset(&currentInterface, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
			currentInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
			for (i = 0; SetupDiEnumDeviceInterfaces(usbDevices, NULL, guids[k], i, &currentInterface); i++) {
				DWORD requiredSize = 0;
				PSP_DEVICE_INTERFACE_DETAIL_DATA_A details;
				SetupDiGetDeviceInterfaceDetailA(usbDevices, &currentInterface, NULL, 0, &requiredSize, NULL);
				// device may have vanished between enumeration and detail query
				if (requiredSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A)) {
					continue;
				}
				details = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A) malloc(requiredSize);
				if (!details) {
					continue;
				}
				details->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
				SP_DEVINFO_DATA devinfodata;
				devinfodata.cbSize = sizeof(SP_DEVINFO_DATA);
				if (!SetupDiGetDeviceInterfaceDetailA(usbDevices, &currentInterface, details, requiredSize, NULL, &devinfodata)) {
					free(details);
					continue;
				}

				DWORD sz = REG_SZ;
				char driver[256];
				driver[0] = '\0';
				if (!SetupDiGetDeviceRegistryPropertyA(usbDevices, &devinfodata, SPDRP_DRIVER, &sz, (PBYTE)driver, sizeof(driver), NULL)) {
					debug("%s: ERROR: Failed to get driver key\n", __func__);
					free(details);
					continue;
				}

				char *p = strrchr(driver, '\\');
				if (!p) {
					debug("%s: ERROR: Failed to parse device location\n", __func__);
					free(details);
					continue;
				}
				p++;
				uint32_t location = 0;
				if (!*p || strlen(p) < 4) {
					debug("%s: ERROR: Driver location suffix too short\n", __func__);
					free(details);
					continue;
				}
				memcpy(&location, p, 4);
				int found = 0;

				mutex_lock(&device_mutex);
				FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
					if (devinfo->location == location) {
						devinfo->alive = 1;
						found = 1;
						break;
					}
				} ENDFOREACH
				mutex_unlock(&device_mutex);

				unsigned int pid = 0;
				unsigned int vid = 0;
				if (sscanf(details->DevicePath, "\\\\?\\%*3s#vid_%04x&pid_%04x", &vid, &pid)!= 2) {
					debug("%s: ERROR: failed to parse VID/PID! path: %s\n", __func__, details->DevicePath);
					free(details);
					continue;
				}
				if (vid != APPLE_VENDOR_ID) {
					free(details);
					continue;
				}

				// make sure the current device is actually in the right mode for the given driver interface
				int skip = 0;
				if ((guids[k] == &GUID_DEVINTERFACE_DFU && pid != IRECV_K_DFU_MODE && pid != IRECV_K_WTF_MODE)
				    || (guids[k] == &GUID_DEVINTERFACE_PORTDFU && pid != IRECV_K_PORT_DFU_MODE)
				    || (guids[k] == &GUID_DEVINTERFACE_IBOOT && (pid < IRECV_K_RECOVERY_MODE_1 || pid > IRECV_K_RECOVERY_MODE_4))
				    || (guids[k] == &GUID_DEVINTERFACE_KIS && pid != 1)
				) {
					skip = 1;
				}

				if (!found && !skip) {
					// Add device to newDevices list, and deliver the notification later, when removed devices are first handled.
					struct irecv_win_dev_ctx *win_ctx = (struct irecv_win_dev_ctx*)malloc(sizeof(struct irecv_win_dev_ctx));
					win_ctx->details = details;
					win_ctx->location = location;
					collection_add(&newDevices, win_ctx);
					details = NULL;
				}
				free(details);
			}
			SetupDiDestroyDeviceInfoList(usbDevices);
		}

		_irecv_remove_dead_devices();

		// handle newly added devices and remove from local list
		FOREACH(struct irecv_win_dev_ctx *win_ctx, &newDevices) {
			debug("%s: found new: %s, location: %d\n", __func__, win_ctx->details->DevicePath, win_ctx->location);
			_irecv_handle_device_add(win_ctx);
			free(win_ctx->details);
			collection_remove(&newDevices, win_ctx);
			free(win_ctx);
		} ENDFOREACH

		Sleep(500);
		mutex_lock(&listener_mutex);
		if (collection_count(&listeners) == 0) {
			running = 0;
		}
		mutex_unlock(&listener_mutex);
	} while (running);

	collection_free(&newDevices);
#else /* !_WIN32 */
#ifdef HAVE_IOKIT
	kern_return_t kr;

	IONotificationPortRef notifyPort = IONotificationPortCreate(MACH_PORT_NULL);
	CFRunLoopSourceRef runLoopSource = IONotificationPortGetRunLoopSource(notifyPort);
	iokit_runloop = CFRunLoopGetCurrent();
	CFRunLoopAddSource(iokit_runloop, runLoopSource, kCFRunLoopDefaultMode);

	uint16_t pids[9] = { IRECV_K_WTF_MODE, IRECV_K_DFU_MODE, IRECV_K_RECOVERY_MODE_1, IRECV_K_RECOVERY_MODE_2, IRECV_K_RECOVERY_MODE_3, IRECV_K_RECOVERY_MODE_4, IRECV_K_PORT_DFU_MODE, KIS_PRODUCT_ID, 0 };
	int i = 0;
	while (pids[i] > 0) {
		CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
		iokit_cfdictionary_set_short(matchingDict, CFSTR(kUSBVendorID), kAppleVendorID);
		iokit_cfdictionary_set_short(matchingDict, CFSTR(kUSBProductID), pids[i]);

		matchingDict = (CFMutableDictionaryRef)CFRetain(matchingDict);

		io_iterator_t devAddedIter;
		kr = IOServiceAddMatchingNotification(notifyPort, kIOFirstMatchNotification, matchingDict, iokit_device_added, NULL, &devAddedIter);
		if (kr != kIOReturnSuccess) {
			debug("%s: Failed to register device add notification callback\n", __func__);
		}
		iokit_device_added(NULL, devAddedIter);

		io_iterator_t devRemovedIter;
		kr = IOServiceAddMatchingNotification(notifyPort, kIOTerminatedNotification, matchingDict, iokit_device_removed, NULL, &devRemovedIter);
		if (kr != kIOReturnSuccess) {
			debug("%s: Failed to register device remove notification callback\n", __func__);
		}
		iokit_device_removed(NULL, devRemovedIter);

		i++;
	}

	mutex_lock(&(info->startup_mutex));
	cond_signal(&(info->startup_cond));
	mutex_unlock(&(info->startup_mutex));

	CFRunLoopRun();

#else /* !HAVE_IOKIT */
#ifdef HAVE_LIBUSB_HOTPLUG_API
	static libusb_hotplug_callback_handle usb_hotplug_cb_handle;
	kis_watch_start();
	libusb_hotplug_register_callback(irecv_hotplug_ctx, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, LIBUSB_HOTPLUG_ENUMERATE, APPLE_VENDOR_ID, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, _irecv_usb_hotplug_cb, NULL, &usb_hotplug_cb_handle);
	int running = 1;

	mutex_lock(&(info->startup_mutex));
	cond_signal(&(info->startup_cond));
	mutex_unlock(&(info->startup_mutex));

	do {
		struct timeval tv;
		tv.tv_sec = tv.tv_usec = 0;
		libusb_handle_events_timeout(irecv_hotplug_ctx, &tv);
		kis_watch_poll();

		mutex_lock(&listener_mutex);
		if (collection_count(&listeners) == 0) {
			running = 0;
		}
		mutex_unlock(&listener_mutex);

		usleep(100000);
	} while (running);
	libusb_hotplug_deregister_callback(irecv_hotplug_ctx, usb_hotplug_cb_handle);
	kis_watch_clear();
	collection_free(&kis_watches);
#else /* !HAVE_LIBUSB_HOTPLUG_API */
	int i, cnt;
	libusb_device **devs;
	int running = 1;

	mutex_lock(&(info->startup_mutex));
	cond_signal(&(info->startup_cond));
	mutex_unlock(&(info->startup_mutex));

	do {
		cnt = libusb_get_device_list(irecv_hotplug_ctx, &devs);
		if (cnt < 0) {
			debug("%s: FATAL: Failed to get device list: %s\n", __func__, libusb_error_name(cnt));
			return NULL;
		}

		mutex_lock(&device_mutex);
		FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
			devinfo->alive = 0;
		} ENDFOREACH
		mutex_unlock(&device_mutex);

		for (i = 0; i < cnt; i++) {
			libusb_device *dev = devs[i];
			if (!_irecv_is_recovery_device(dev)) {
				continue;
			}
			uint8_t bus = libusb_get_bus_number(dev);
			uint8_t address = libusb_get_device_address(dev);
			uint32_t location = (bus << 16) | address;
			int found = 0;
			mutex_lock(&device_mutex);
			FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
				if (devinfo->location == location) {
					devinfo->alive = 1;
					found = 1;
					break;
				}
			} ENDFOREACH
			mutex_unlock(&device_mutex);
			if (!found) {
				_irecv_handle_device_add(dev);
			}
		}

		_irecv_remove_dead_devices();

		libusb_free_device_list(devs, 1);

		mutex_lock(&listener_mutex);
		if (collection_count(&listeners) == 0) {
			running = 0;
		}
		mutex_unlock(&listener_mutex);
		if (!running)
			break;
		usleep(500000);
	} while (running);
#endif /* !HAVE_LIBUSB_HOTPLUG_API */
#endif /* !HAVE_IOKIT */
#endif /* !_WIN32 */
	return NULL;
}
#endif /* !USE_DUMMY */

irecv_error_t irecv_device_event_subscribe(irecv_device_event_context_t *context, irecv_device_event_cb_t callback, void *user_data)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (!context || !callback)
		return IRECV_E_INVALID_INPUT;

	struct irecv_device_event_context* _context = malloc(sizeof(struct irecv_device_event_context));
	if (!_context) {
		return IRECV_E_OUT_OF_MEMORY;
	}

	_context->callback = callback;
	_context->user_data = user_data;

	mutex_lock(&listener_mutex);
	collection_add(&listeners, _context);

	if (th_event_handler == THREAD_T_NULL || !thread_alive(th_event_handler)) {
		mutex_unlock(&listener_mutex);
		struct _irecv_event_handler_info info;
		cond_init(&info.startup_cond);
		mutex_init(&info.startup_mutex);
#ifndef _WIN32
#ifndef HAVE_IOKIT
		libusb_init(&irecv_hotplug_ctx);
#endif
#endif
		collection_init(&devices);
		mutex_init(&device_mutex);
		mutex_lock(&info.startup_mutex);
		if (thread_new(&th_event_handler, _irecv_event_handler, &info) == 0) {
			cond_wait(&info.startup_cond, &info.startup_mutex);
		}
		mutex_unlock(&info.startup_mutex);
		cond_destroy(&info.startup_cond);
		mutex_destroy(&info.startup_mutex);
	} else {
		/* send DEVICE_ADD events to the new listener */
		mutex_lock(&device_mutex);
		FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
			if (devinfo && devinfo->alive) {
				irecv_device_event_t ev;
				ev.type = IRECV_DEVICE_ADD;
				ev.mode = devinfo->mode;
				ev.device_info = &(devinfo->device_info);
				_context->callback(&ev, _context->user_data);
			}
		} ENDFOREACH
		mutex_unlock(&device_mutex);
		mutex_unlock(&listener_mutex);
	}

	*context = _context;

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_device_event_unsubscribe(irecv_device_event_context_t context)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (!context)
		return IRECV_E_INVALID_INPUT;

	mutex_lock(&listener_mutex);
	collection_remove(&listeners, context);
	int num = collection_count(&listeners);
	mutex_unlock(&listener_mutex);

	if (num == 0 && th_event_handler != THREAD_T_NULL && thread_alive(th_event_handler)) {
#ifdef HAVE_IOKIT
		if (iokit_runloop) {
			while (!CFRunLoopIsWaiting(iokit_runloop)) usleep(420);
			CFRunLoopStop(iokit_runloop);
			iokit_runloop = NULL;
		}
#endif
		thread_join(th_event_handler);
		thread_free(th_event_handler);
		th_event_handler = THREAD_T_NULL;
		mutex_lock(&device_mutex);
		FOREACH(struct irecv_usb_device_info *devinfo, &devices) {
			free(devinfo->device_info.srnm);
			devinfo->device_info.srnm = NULL;
			free(devinfo->device_info.imei);
			devinfo->device_info.imei = NULL;
			free(devinfo->device_info.srtg);
			devinfo->device_info.srtg = NULL;
			free(devinfo->device_info.serial_string);
			devinfo->device_info.serial_string = NULL;
			free(devinfo);
		} ENDFOREACH
		collection_free(&devices);
		mutex_unlock(&device_mutex);
		mutex_destroy(&device_mutex);
#ifndef _WIN32
#ifndef HAVE_IOKIT
		libusb_exit(irecv_hotplug_ctx);
		irecv_hotplug_ctx = NULL;
#endif
#endif
	}

	free(context);

	return IRECV_E_SUCCESS;
#endif
}

static irecv_error_t irecv_cleanup(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (client != NULL) {
		if (client->disconnected_callback != NULL) {
			irecv_event_t event;
			event.size = 0;
			event.data = NULL;
			event.progress = 0;
			event.type = IRECV_DISCONNECTED;
			client->disconnected_callback(client, &event);
		}
#ifndef _WIN32
#ifdef HAVE_IOKIT
		if (client->usbInterface) {
			(*client->usbInterface)->USBInterfaceClose(client->usbInterface);
			(*client->usbInterface)->Release(client->usbInterface);
			client->usbInterface = NULL;
		}
		if (client->handle) {
			(*client->handle)->USBDeviceClose(client->handle);
			(*client->handle)->Release(client->handle);
			client->handle = NULL;
		}
		if(client->async_event_source) {
			CFRunLoopRemoveSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
			CFRelease(client->async_event_source);
		}
#else
		if (client->kis) {
			kis_link_put_client(client->kis);
			client->kis = NULL;
			client->handle = NULL;
		}
		if (client->handle != NULL) {
			if ((client->mode != IRECV_K_DFU_MODE) && (client->mode != IRECV_K_PORT_DFU_MODE) && (client->mode != IRECV_K_WTF_MODE) && (client->isKIS == 0)) {
				libusb_release_interface(client->handle, client->usb_interface);
			}
			libusb_close(client->handle);
			client->handle = NULL;
		}
#endif
#else
		if (client->handle != NULL && client->handle != INVALID_HANDLE_VALUE) {
			CloseHandle(client->handle);
		}
		client->handle = INVALID_HANDLE_VALUE;
#endif
		free(client->device_info.srnm);
		free(client->device_info.imei);
		free(client->device_info.srtg);
		free(client->device_info.serial_string);
		free(client->device_info.ap_nonce);
		free(client->device_info.sep_nonce);
		// irecv_reconnect() may clean up a client that is later closed again
		client->device_info.srnm = NULL;
		client->device_info.imei = NULL;
		client->device_info.srtg = NULL;
		client->device_info.serial_string = NULL;
		client->device_info.ap_nonce = NULL;
		client->device_info.sep_nonce = NULL;
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_close(irecv_client_t client)
{
	irecv_error_t ret = IRECV_E_SUCCESS;
	if (client) {
		ret = irecv_cleanup(client);
		free(client);
		client = NULL;
	}
	return ret;
}

void irecv_set_debug_level(int level)
{
	libirecovery_debug = level;
	irecv_devicedb_set_debug(level);
#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
	if (libirecovery_context) {
#if LIBUSB_API_VERSION >= 0x01000106
		libusb_set_option(libirecovery_context, LIBUSB_OPTION_LOG_LEVEL, libirecovery_debug > 2 ? 1: 0);
#else
		libusb_set_debug(libirecovery_context, libirecovery_debug > 2 ? 1: 0);
#endif
	}
#endif
#endif
#endif
}

const char* irecv_version()
{
#ifndef PACKAGE_VERSION
#error PACKAGE_VERSION is not defined!
#endif
    return PACKAGE_VERSION;
}


int irecv_command_is_breq(const char* command)
{
	static const char* const commands[] = { "go", "bootx", "reboot", "memboot" };
	size_t token_length;
	size_t i;

	if (command == NULL) {
		return 0;
	}

	command += strspn(command, " \t\r\n");
	token_length = strcspn(command, " \t\r\n");
	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		if (strlen(commands[i]) == token_length
			&& strncmp(command, commands[i], token_length) == 0) {
			return 1;
		}
	}

	return 0;
}

#ifndef USE_DUMMY
static int irecv_command_error_is_accepted(irecv_error_t error, const char* command)
{
	if (error == IRECV_E_PIPE) {
		return 1;
	}

	return irecv_command_is_breq(command)
		&& (error == IRECV_E_NO_DEVICE
			|| error == IRECV_E_TIMEOUT
			|| error == IRECV_E_USB_STATUS);
}

static irecv_error_t irecv_control_transfer_error(int result)
{
#ifdef HAVE_IOKIT
	/* The IOKit backend already returns public irecv_error_t values. */
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
#elif defined(_WIN32)
	/* The Windows backend exposes only an undifferentiated negative result. */
	return IRECV_E_USB_STATUS;
#else
	switch (result) {
	case LIBUSB_ERROR_PIPE:
		return IRECV_E_PIPE;
	case LIBUSB_ERROR_TIMEOUT:
		return IRECV_E_TIMEOUT;
	case LIBUSB_ERROR_NO_DEVICE:
		return IRECV_E_NO_DEVICE;
	case LIBUSB_ERROR_NO_MEM:
		return IRECV_E_OUT_OF_MEMORY;
	default:
		return IRECV_E_USB_STATUS;
	}
#endif
}

static irecv_error_t irecv_send_command_raw(irecv_client_t client, const char* command, uint8_t b_request)
{
	unsigned int length = strlen(command);
	if (length >= 0x100) {
		return IRECV_E_INVALID_INPUT;
	}

	if (length > 0 && client->device_info.cpid == 0x8900 && !client->device_info.ecid) {
		int bytes = 0;
		irecv_error_t error = 0;
#ifdef DEBUG
		uint8_t buf[0x100] = {0x00, 0x00, 0x34, 0x12}; // ask how large commands should be
		if ((error = irecv_usb_interrupt_transfer(client, 0x04, &buf[0], 4, &bytes, USB_TIMEOUT))) return error;
		if ((error = irecv_usb_interrupt_transfer(client, 0x83, &buf[0], sizeof(buf), &bytes, USB_TIMEOUT))) return error;
		if (bytes != sizeof(legacyCMD)) return IRECV_E_UNKNOWN_ERROR;
#endif
		char cmdstr[0x100] = {0};
		if (length & 0xf) {
			length &= ~0xf;
			length += 0x10;
		}
		snprintf(cmdstr, sizeof(cmdstr), "%s\n",command);
		legacyCMD cmd = {
			MSG_SEND_COMMAND,
			0x1234, //magic
			(uint32_t)length, //zero terminated?
			0x0
		};
		if ((error = irecv_usb_interrupt_transfer(client, 0x04, (unsigned char*)&cmd, sizeof(cmd), &bytes, USB_TIMEOUT))) return error;
		if ((error = irecv_usb_interrupt_transfer(client, 0x83, (unsigned char*)&cmd, sizeof(cmd), &bytes, USB_TIMEOUT))) return error;
		if (cmd.cmdcode != MSG_ACK) return IRECV_E_UNKNOWN_ERROR;
		if ((error = irecv_usb_interrupt_transfer(client, 0x02, (unsigned char*)cmdstr, length, &bytes, USB_TIMEOUT))) return error;
		sleep(1); //go easy on this old device
		return IRECV_E_SUCCESS;
	}

	if (length > 0) {
		int transferred = irecv_usb_control_transfer(client, 0x40, b_request, 0, 0, (unsigned char*) command, length + 1, USB_TIMEOUT);
		if (transferred < 0) {
			return irecv_control_transfer_error(transferred);
		}
		if ((unsigned int)transferred != length + 1) {
			return IRECV_E_USB_STATUS;
		}
	}

	return IRECV_E_SUCCESS;
}
#endif

irecv_error_t irecv_send_command_breq(irecv_client_t client, const char* command, uint8_t b_request)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	irecv_error_t error = 0;

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	unsigned int length = strlen(command);
	if (length >= 0x100) {
		return IRECV_E_INVALID_INPUT;
	}

	irecv_event_t event;
	if (client->precommand_callback != NULL) {
		event.size = length;
		event.data = command;
		event.type = IRECV_PRECOMMAND;
		if (client->precommand_callback(client, &event)) {
			return IRECV_E_SUCCESS;
		}
	}

	error = irecv_send_command_raw(client, command, b_request);
	if (error != IRECV_E_SUCCESS) {
		debug("Failed to send command %s\n", command);
		if (!irecv_command_error_is_accepted(error, command)) {
			return error;
		}
	}

	if (client->postcommand_callback != NULL) {
		event.size = length;
		event.data = command;
		event.type = IRECV_POSTCOMMAND;
		if (client->postcommand_callback(client, &event)) {
			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_send_command(irecv_client_t client, const char* command)
{
	return irecv_send_command_breq(client, command, 0);
}

irecv_error_t irecv_send_file(irecv_client_t client, const char* filename, unsigned int options)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		return IRECV_E_FILE_NOT_FOUND;
	}

	struct stat fst;
	if (fstat(fileno(file), &fst) < 0) {
		return IRECV_E_UNKNOWN_ERROR;
	}
	size_t length = fst.st_size;

	char* buffer = (char*)malloc(length);
	if (buffer == NULL) {
		fclose(file);
		return IRECV_E_OUT_OF_MEMORY;
	}

	size_t bytes = fread(buffer, 1, length, file);
	fclose(file);

	if (bytes != length) {
		free(buffer);
		return IRECV_E_UNKNOWN_ERROR;
	}

	irecv_error_t error = irecv_send_buffer(client, (unsigned char*)buffer, length, options);
	free(buffer);

	return error;
#endif
}

#ifndef USE_DUMMY
static irecv_error_t irecv_get_status(irecv_client_t client, unsigned int* status)
{
	if (check_context(client) != IRECV_E_SUCCESS) {
		*status = 0;
		return IRECV_E_NO_DEVICE;
	}

	unsigned char buffer[6];
	memset(buffer, 0, sizeof(buffer));
	if (irecv_usb_control_transfer(client, 0xA1, 3, 0, 0, buffer, sizeof(buffer), USB_TIMEOUT) != sizeof(buffer)) {
		*status = 0;
		return IRECV_E_USB_STATUS;
	}

	*status = (unsigned int) buffer[4];

	return IRECV_E_SUCCESS;
}

static irecv_error_t irecv_kis_send_buffer(irecv_client_t client, unsigned char* buffer, unsigned long length, unsigned int options)
{
	if (client->mode != IRECV_K_DFU_MODE) {
		return IRECV_E_UNSUPPORTED;
	}
#ifdef HAVE_KIS_LINKS
	if (client->kis) {
		kis_link_drove(client->kis);
	}
#endif

	unsigned long origLen = length;

	KIS_upload_chunk *chunk = calloc(1, sizeof(KIS_upload_chunk));
	uint64_t address = 0;
	while (length) {
		unsigned long toUpload = length;
		if (toUpload > 0x4000)
			toUpload = 0x4000;

#ifdef _WIN32
		memcpy(chunk->data, buffer, toUpload);
		chunk->size    = toUpload;
		chunk->address = address;
#else
		irecv_error_t error = irecv_kis_request_init(&chunk->hdr, KIS_PORTAL_RSM, KIS_INDEX_UPLOAD, 3, toUpload, 0);
		if (error != IRECV_E_SUCCESS) {
			free(chunk);
			debug("Failed to init chunk header, error %d\n", error);
			return error;
		}

		chunk->address = address;
		chunk->size    = toUpload;
		memcpy(chunk->data, buffer, toUpload);
#endif

#ifdef _WIN32
		DWORD transferred = 0;
		int ret = win32_device_io_control(client->handle, 0x220008, chunk, sizeof(*chunk), NULL, 0, &transferred, USB_TIMEOUT);
		irecv_error_t error = (ret) ? IRECV_E_SUCCESS : IRECV_E_USB_UPLOAD;
#else
		KIS_generic_reply reply;
		size_t rcvSize = sizeof(reply);
		error = irecv_kis_request(client, &chunk->hdr, sizeof(*chunk) - (0x4000 - toUpload), &reply.hdr, &rcvSize);
#endif
		if (error != IRECV_E_SUCCESS) {
			free(chunk);
			debug("Failed to upload chunk, error %d\n", error);
			return error;
		}

		address += toUpload;
		buffer  += toUpload;
		length  -= toUpload;

		if (client->progress_callback != NULL) {
			irecv_event_t event;
			event.progress = ((double) (origLen - length) / (double) origLen) * 100.0;
			event.type = IRECV_PROGRESS;
			event.data = (char*)"Uploading";
			event.size = origLen - length;
			client->progress_callback(client, &event);
		} else {
			debug("Sent: %lu bytes - %lu of %lu\n", toUpload, origLen - length, origLen);
		}
	}
	free(chunk);

	if (options & IRECV_SEND_OPT_DFU_NOTIFY_FINISH) {
#ifdef _WIN32
		DWORD amount = (DWORD)origLen;
		DWORD transferred = 0;
		int ret = win32_device_io_control(client->handle, 0x22000C, &amount, 4, NULL, 0, &transferred, USB_TIMEOUT);
		irecv_error_t error = (ret) ? IRECV_E_SUCCESS : IRECV_E_USB_UPLOAD;
#else
		irecv_error_t error = irecv_kis_config_write32(client, KIS_PORTAL_RSM, KIS_INDEX_BOOT_IMG, origLen);
#endif
		if (error != IRECV_E_SUCCESS) {
			debug("Failed to boot image, error %d\n", error);
			return error;
		}
	}

	return IRECV_E_SUCCESS;
}
#endif

irecv_error_t irecv_send_buffer(irecv_client_t client, unsigned char* buffer, unsigned long length, unsigned int options)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (client->isKIS)
		return irecv_kis_send_buffer(client, buffer, length, options);

	irecv_error_t error = 0;
	int recovery_mode = ((client->mode != IRECV_K_DFU_MODE) && (client->mode != IRECV_K_PORT_DFU_MODE) && (client->mode != IRECV_K_WTF_MODE));
	int legacy_recovery_mode = 0;
	int isiOS2 = 0;

	if (recovery_mode && client->device_info.cpid == 0x8900 && !client->device_info.ecid) {
#ifdef DEBUG
		uint8_t buf[0x100] = {0x00, 0x00, 0x34, 0x12}; // ask how large commands should be
		int bytes = 0;
		if ((error = irecv_usb_interrupt_transfer(client, 0x04, &buf[0], 4, &bytes, USB_TIMEOUT))) return error;
		if ((error = irecv_usb_interrupt_transfer(client, 0x83, &buf[0], sizeof(buf), &bytes, USB_TIMEOUT))) return error;
		if (bytes != sizeof(legacyCMD)) return IRECV_E_UNKNOWN_ERROR;
#endif
		legacy_recovery_mode = 1;
	}

	if (recovery_mode && !legacy_recovery_mode) {
		// we are in recovery mode and we are not dealing with iOS 1.x
		if ((client->device_info.cpid == 0x8900 || client->device_info.cpid == 0x8720) && !client->device_info.have_ibfl) {
			// iOS 2.x doesn't have IBFL tag, but iOS 3 does
			// Also, to avoid false activation of this codepath, restrict it to the only two CPID which can run iOS 2
			recovery_mode = 0; // iOS 2 recovery mode works same as DFU mode
			isiOS2 = 1;
			options |= IRECV_SEND_OPT_DFU_NOTIFY_FINISH;
		}
	}

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	unsigned int h1 = 0xFFFFFFFF;
	unsigned char dfu_xbuf[12] = {0xff, 0xff, 0xff, 0xff, 0xac, 0x05, 0x00, 0x01, 0x55, 0x46, 0x44, 0x10};
	int dfu_crc = 1;
	int packet_size = recovery_mode ? 0x8000 : 0x800;
	if (legacy_recovery_mode) packet_size = 0x200;
	if (!recovery_mode && (options & IRECV_SEND_OPT_DFU_SMALL_PKT)) {
		packet_size = 0x40;
		dfu_crc = 0;
	}
	int last = length % packet_size;
	int packets = length / packet_size;

	if (last != 0) {
		packets++;
	} else {
		last = packet_size;
	}

	/* initiate transfer */
	if (legacy_recovery_mode) {
		int bytes = 0;
		uint32_t loadaddr0x8900 = 0x09000000;
		const char *ios1_overwrite_loadaddr = getenv("LIBIRECOVERY_IOS1_OVERWRITE_LOADADDR");
		if (ios1_overwrite_loadaddr) {
			sscanf(ios1_overwrite_loadaddr, "0x%x",&loadaddr0x8900);
			debug("Overwriting loadaddr requested by env var. uploading to 0x%08x\n",loadaddr0x8900);
		}
		legacyCMD cmd = {
			MSG_SEND_FILE,
			0x1234, //magic
			(uint32_t)length,
			loadaddr0x8900
		};
		if ((error = irecv_usb_interrupt_transfer(client, 0x04, (unsigned char*)&cmd, sizeof(cmd), &bytes, USB_TIMEOUT))) return error;
		if ((error = irecv_usb_interrupt_transfer(client, 0x83, (unsigned char*)&cmd, sizeof(cmd), &bytes, USB_TIMEOUT))) return error;
		if (cmd.cmdcode != MSG_ACK) return IRECV_E_UNKNOWN_ERROR;
	} else if (recovery_mode) {
		error = irecv_usb_control_transfer(client, 0x41, 0, 0, 0, NULL, 0, USB_TIMEOUT);
	} else {
		uint8_t state = 0;
		if (irecv_usb_control_transfer(client, 0xa1, 5, 0, 0, (unsigned char*)&state, 1, USB_TIMEOUT) == 1) {
			error = IRECV_E_SUCCESS;
		} else {
			return IRECV_E_USB_UPLOAD;
		}
		switch (state) {
		case 2:
			/* DFU IDLE */
			break;
		case 8:
			/* DFU WAIT RESET */
			if (!isiOS2) {
				debug("Unexpected state %d in non-iOS2 mode!, issuing ABORT\n", state);
				irecv_usb_control_transfer(client, 0x21, 6, 0, 0, NULL, 0, USB_TIMEOUT);
				error = IRECV_E_USB_UPLOAD;
			}
			break;
		case 10:
			debug("DFU ERROR, issuing CLRSTATUS\n");
			irecv_usb_control_transfer(client, 0x21, 4, 0, 0, NULL, 0, USB_TIMEOUT);
			error = IRECV_E_USB_UPLOAD;
			break;
		default:
			debug("Unexpected state %d, issuing ABORT\n", state);
			irecv_usb_control_transfer(client, 0x21, 6, 0, 0, NULL, 0, USB_TIMEOUT);
			error = IRECV_E_USB_UPLOAD;
			break;
		}
	}

	if (error != IRECV_E_SUCCESS) {
		return error;
	}

	int i = 0;
	unsigned long count = 0;
	unsigned int status = 0;
	int bytes = 0;
	for (i = 0; i < packets; i++) {
		int size = (i + 1) < packets ? packet_size : last;

		if (legacy_recovery_mode) {
			// Use interrupt transfer for legacy devices
			error = irecv_usb_interrupt_transfer(client, 0x05, &buffer[i * packet_size], size, &bytes, USB_TIMEOUT);
		} else if (recovery_mode) {
			// Use bulk transfer for recovery mode
			error = irecv_usb_bulk_transfer(client, 0x04, &buffer[i * packet_size], size, &bytes, USB_TIMEOUT);
		} else {
			// Use control transfer for DFU and WTF mode
			if (dfu_crc) {
				int j;
				for (j = 0; j < size; j++) {
					crc32_step(h1, buffer[i*packet_size + j]);
				}
			}
			if (dfu_crc && i+1 == packets) {
				int j;
				if (size+16 > packet_size) {
					bytes = irecv_usb_control_transfer(client, 0x21, 1, i, 0, &buffer[i * packet_size], size, USB_TIMEOUT);
					if (bytes != size) {
						return IRECV_E_USB_UPLOAD;
					}
					count += size;
					size = 0;
				}
				for (j = 0; j < 2; j++) {
					crc32_step(h1, dfu_xbuf[j*6 + 0]);
					crc32_step(h1, dfu_xbuf[j*6 + 1]);
					crc32_step(h1, dfu_xbuf[j*6 + 2]);
					crc32_step(h1, dfu_xbuf[j*6 + 3]);
					crc32_step(h1, dfu_xbuf[j*6 + 4]);
					crc32_step(h1, dfu_xbuf[j*6 + 5]);
				}

				char* newbuf = (char*)malloc(size + 16);
				if (size > 0) {
					memcpy(newbuf, &buffer[i * packet_size], size);
				}
				memcpy(newbuf+size, dfu_xbuf, 12);
				newbuf[size+12] = h1 & 0xFF;
				newbuf[size+13] = (h1 >> 8) & 0xFF;
				newbuf[size+14] = (h1 >> 16) & 0xFF;
				newbuf[size+15] = (h1 >> 24) & 0xFF;
				size += 16;
				bytes = irecv_usb_control_transfer(client, 0x21, 1, i, 0, (unsigned char*)newbuf, size, USB_TIMEOUT);
				free(newbuf);
			} else {
				bytes = irecv_usb_control_transfer(client, 0x21, 1, i, 0, &buffer[i * packet_size], size, USB_TIMEOUT);
			}
		}

		if (bytes != size) {
			return IRECV_E_USB_UPLOAD;
		}

		if (!recovery_mode) {
			error = irecv_get_status(client, &status);
		}

		if (error != IRECV_E_SUCCESS) {
			return error;
		}

		if (!recovery_mode && status != 5) {
			int retry = 0;

			while (retry++ < 20) {
				irecv_get_status(client, &status);
				if (status == 5) {
					break;
				}
				sleep(1);
			}

			if (status != 5) {
				return IRECV_E_USB_UPLOAD;
			}
		}

		count += size;
		if (client->progress_callback != NULL) {
			irecv_event_t event;
			event.progress = ((double) count/ (double) length) * 100.0;
			event.type = IRECV_PROGRESS;
			event.data = (char*)"Uploading";
			event.size = count;
			client->progress_callback(client, &event);
		} else {
			debug("Sent: %d bytes - %lu of %lu\n", bytes, count, length);
		}
	}

	if (recovery_mode && length % 512 == 0) {
		/* send a ZLP */
		bytes = 0;
		irecv_usb_bulk_transfer(client, 0x04, buffer, 0, &bytes, USB_TIMEOUT);
	}

	if ((options & IRECV_SEND_OPT_DFU_NOTIFY_FINISH) && !recovery_mode) {
		irecv_usb_control_transfer(client, 0x21, 1, packets, 0, (unsigned char*) buffer, 0, USB_TIMEOUT);

		for (i = 0; i < 2; i++) {
			error = irecv_get_status(client, &status);
			if (error != IRECV_E_SUCCESS) {
				return error;
			}
		}

		if ((options & IRECV_SEND_OPT_DFU_FORCE_ZLP)) {
			/* we send a pseudo ZLP here just in case */
			irecv_usb_control_transfer(client, 0x21, 1, 0, 0, 0, 0, USB_TIMEOUT);
		}

		irecv_reset(client);

		if (isiOS2) {
			irecv_reconnect(client, 0);
		}
	}

	if (legacy_recovery_mode) {
		irecv_reconnect(client, 0);
		char cmdstr[0x100] = {0};
		snprintf(cmdstr, sizeof(cmdstr), "setenv filesize %d", (int)length);
		irecv_send_command(client, cmdstr);
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_receive(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	char buffer[BUFFER_SIZE];
	memset(buffer, 0, BUFFER_SIZE);

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	int bytes = 0;
	while (1) {
		irecv_usb_set_interface(client, 1, 1);
		int r = irecv_usb_bulk_transfer(client, 0x81, (unsigned char*) buffer, BUFFER_SIZE, &bytes, 500);
		irecv_usb_set_interface(client, 0, 0);
		if (r != 0) {
			break;
		}
		if (bytes > 0) {
			if (client->received_callback != NULL) {
				irecv_event_t event;
				event.size = bytes;
				event.data = buffer;
				event.type = IRECV_RECEIVED;
				if (client->received_callback(client, &event) != 0) {
					break;
				}
			}
		} else break;
	}
	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_getenv(irecv_client_t client, const char* variable, char** value)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	char command[256];

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	*value = NULL;

	if (variable == NULL) {
		return IRECV_E_INVALID_INPUT;
	}

	if (client->device_info.cpid == 0x8900 && !client->device_info.ecid) {
		debug("iOS 1 doesn't support getenv\n");
		return IRECV_E_UNSUPPORTED;
	}

	memset(command, 0, sizeof(command));
	snprintf(command, sizeof(command)-1, "getenv %s", variable);
	irecv_error_t error = irecv_send_command_raw(client, command, 0);
	if (error != IRECV_E_SUCCESS) {
		return error;
	}

	int rsize = 256;
	char* response = (char*) malloc(rsize);
	if (response == NULL) {
		return IRECV_E_OUT_OF_MEMORY;
	}

	int received = irecv_usb_control_transfer(client, 0xC0, 0, 0, 0, (unsigned char*) response, rsize-1, USB_TIMEOUT);
	if (received < 0) {
		free(response);
		return irecv_control_transfer_error(received);
	}
	if (received >= rsize) {
		free(response);
		return IRECV_E_USB_STATUS;
	}
	response[received] = '\0';

	/* A zero-length response is a successful getenv with an empty value. */
	*value = response;

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_getret(irecv_client_t client, unsigned int* value)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	*value = 0;

	int rsize = 256;
	char* response = (char*) malloc(rsize);
	if (response == NULL) {
		return IRECV_E_OUT_OF_MEMORY;
	}

	memset(response, 0, rsize);
	irecv_usb_control_transfer(client, 0xC0, 0, 0, 0, (unsigned char*) response, rsize-1, USB_TIMEOUT);

	*value = (unsigned int) *response;

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_get_mode(irecv_client_t client, int* mode)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	*mode = client->mode;

	return IRECV_E_SUCCESS;
#endif
}

const struct irecv_device_info* irecv_get_device_info(irecv_client_t client)
{
#ifdef USE_DUMMY
	return NULL;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return NULL;

	return &client->device_info;
#endif
}

#ifndef USE_DUMMY
#ifdef HAVE_IOKIT
static void *iokit_limera1n_usb_submit_request(void *argv)
{
	void **args = argv;
	IOUSBDeviceInterface320 **dev = args[0];
	IOUSBDevRequest *req = args[1];

	IOReturn result = (*dev)->DeviceRequest(dev, req);
	if (result != kIOReturnSuccess)
		debug("%s result: %#x\n", __func__, result);

	return NULL;
}
#endif
#endif

irecv_error_t irecv_trigger_limera1n_exploit(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;
#ifdef HAVE_KIS_LINKS
	if (client->kis) {
		return IRECV_E_UNSUPPORTED;
	}
#endif

#ifdef HAVE_IOKIT
	IOReturn result;
	IOUSBDevRequestTO req;
	memset(&req, 0, sizeof(req));

	req.bmRequestType     = 0x21;
	req.bRequest          = 2;
	req.wValue            = 0;
	req.wIndex            = 0;
	req.wLength           = 0;
	req.pData             = NULL;
	req.noDataTimeout     = USB_TIMEOUT;
	req.completionTimeout = USB_TIMEOUT;

	// The original version uses an async request, but we don't have an async event
	// source set up. The hack relies on aborting the transaction before it times out,
	// which can be accomplished by sending on another thread.

	void *args[2] = { client->handle, &req };
	THREAD_T thread;
	thread_new(&thread, iokit_limera1n_usb_submit_request, args);

	usleep(5 * 1000);
	result = (*client->handle)->USBDeviceAbortPipeZero(client->handle);
	if (result != kIOReturnSuccess)
		debug("USBDeviceAbortPipeZero returned %#x\n", result);

	switch (result) {
		case kIOReturnSuccess:         return req.wLenDone;
		case kIOReturnTimeout:         return IRECV_E_TIMEOUT;
		case kIOUSBTransactionTimeout: return IRECV_E_TIMEOUT;
		case kIOReturnNotResponding:   return IRECV_E_NO_DEVICE;
		case kIOReturnNoDevice:	       return IRECV_E_NO_DEVICE;
		default:
			return IRECV_E_UNKNOWN_ERROR;
	}
#else
	irecv_usb_control_transfer(client, 0x21, 2, 0, 0, NULL, 0, USB_TIMEOUT);
#endif

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_execute_script(irecv_client_t client, const char* script)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	irecv_error_t error = IRECV_E_SUCCESS;
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	char* body = strdup(script);
	char* line = strtok(body, "\n");

	while (line != NULL) {
		if (line[0] != '#') {
			error = irecv_send_command(client, line);
			if (error != IRECV_E_SUCCESS) {
				break;
			}

			error = irecv_receive(client);
			if (error != IRECV_E_SUCCESS) {
				break;
			}
		}
		line = strtok(NULL, "\n");
	}

	free(body);

	return error;
#endif
}

irecv_error_t irecv_saveenv(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	irecv_error_t error = irecv_send_command_raw(client, "saveenv", 0);
	if (error != IRECV_E_SUCCESS) {
		return error;
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_setenv(irecv_client_t client, const char* variable, const char* value)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	char command[256];

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	if (variable == NULL || value == NULL) {
		return IRECV_E_UNKNOWN_ERROR;
	}

	memset(command, 0, sizeof(command));
	snprintf(command, sizeof(command)-1, "setenv %s %s", variable, value);
	irecv_error_t error = irecv_send_command_raw(client, command, 0);
	if (error != IRECV_E_SUCCESS) {
		return error;
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_setenv_np(irecv_client_t client, const char* variable, const char* value)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	char command[256];

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	if (variable == NULL || value == NULL) {
		return IRECV_E_UNKNOWN_ERROR;
	}

	memset(command, 0, sizeof(command));
	snprintf(command, sizeof(command)-1, "setenvnp %s %s", variable, value);
	irecv_error_t error = irecv_send_command_raw(client, command, 0);
	if (error != IRECV_E_SUCCESS) {
		return error;
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_reboot(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	irecv_error_t error = irecv_send_command_raw(client, "reboot", 0);
	if (error != IRECV_E_SUCCESS && !irecv_command_error_is_accepted(error, "reboot")) {
		return error;
	}

	return IRECV_E_SUCCESS;
#endif
}

const char* irecv_strerror(irecv_error_t error)
{
	switch (error) {
	case IRECV_E_SUCCESS:
		return "Command completed successfully";

	case IRECV_E_NO_DEVICE:
		return "Unable to find device";

	case IRECV_E_OUT_OF_MEMORY:
		return "Out of memory";

	case IRECV_E_UNABLE_TO_CONNECT:
		return "Unable to connect to device";

	case IRECV_E_INVALID_INPUT:
		return "Invalid input";

	case IRECV_E_FILE_NOT_FOUND:
		return "File not found";

	case IRECV_E_USB_UPLOAD:
		return "Unable to upload data to device";

	case IRECV_E_USB_STATUS:
		return "Unable to get device status";

	case IRECV_E_USB_INTERFACE:
		return "Unable to set device interface";

	case IRECV_E_USB_CONFIGURATION:
		return "Unable to set device configuration";

	case IRECV_E_PIPE:
		return "Broken pipe";

	case IRECV_E_TIMEOUT:
		return "Timeout talking to device";

	case IRECV_E_UNSUPPORTED:
		return "Operation unsupported by driver";

	default:
		return "Unknown error";
	}

	return NULL;
}

irecv_error_t irecv_reset_counters(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	if ((client->mode == IRECV_K_DFU_MODE) || (client->mode == IRECV_K_PORT_DFU_MODE) || (client->mode == IRECV_K_WTF_MODE)) {
		irecv_usb_control_transfer(client, 0x21, 4, 0, 0, 0, 0, USB_TIMEOUT);
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_recv_buffer(irecv_client_t client, char* buffer, unsigned long length)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	int recovery_mode = ((client->mode != IRECV_K_DFU_MODE) && (client->mode != IRECV_K_PORT_DFU_MODE) && (client->mode != IRECV_K_WTF_MODE));

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	int packet_size = recovery_mode ? 0x2000: 0x800;
	int last = length % packet_size;
	int packets = length / packet_size;
	if (last != 0) {
		packets++;
	} else {
		last = packet_size;
	}

	int i = 0;
	int bytes = 0;
	unsigned long count = 0;
	for (i = 0; i < packets; i++) {
		unsigned short size = (i+1) < packets ? packet_size : last;
		bytes = irecv_usb_control_transfer(client, 0xA1, 2, 0, 0, (unsigned char*)&buffer[i * packet_size], size, USB_TIMEOUT);

		if (bytes != size) {
			return IRECV_E_USB_UPLOAD;
		}

		count += size;
		if (client->progress_callback != NULL) {
			irecv_event_t event;
			event.progress = ((double) count/ (double) length) * 100.0;
			event.type = IRECV_PROGRESS;
			event.data = (char*)"Downloading";
			event.size = count;
			client->progress_callback(client, &event);
		} else {
			debug("Sent: %d bytes - %lu of %lu\n", bytes, count, length);
		}
	}

	return IRECV_E_SUCCESS;
#endif
}

irecv_error_t irecv_finish_transfer(irecv_client_t client)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	int i = 0;
	unsigned int status = 0;

	if (check_context(client) != IRECV_E_SUCCESS)
		return IRECV_E_NO_DEVICE;

	irecv_usb_control_transfer(client, 0x21, 1, 0, 0, 0, 0, USB_TIMEOUT);

	for (i = 0; i < 3; i++){
		irecv_get_status(client, &status);
	}

	irecv_reset(client);

	return IRECV_E_SUCCESS;
#endif
}

irecv_device_t irecv_devices_get_all(void)
{
	return irecv_devicedb_get();
}

irecv_error_t irecv_devices_get_device_by_client(irecv_client_t client, irecv_device_t* device)
{
#ifdef USE_DUMMY
	return IRECV_E_UNSUPPORTED;
#else
	int i = 0;
	irecv_device_t table = irecv_devicedb_get();

	if (!client || !device)
		return IRECV_E_INVALID_INPUT;

	*device = NULL;

	if (client->device_info.cpid == 0) {
		return IRECV_E_UNKNOWN_ERROR;
	}

	unsigned int cpid_match = client->device_info.cpid;
	unsigned int bdid_match = client->device_info.bdid;
	if (client->mode == IRECV_K_PORT_DFU_MODE) {
		cpid_match = (client->device_info.bdid >> 8) & 0xFFFF;
		bdid_match = (client->device_info.bdid >> 24) & 0xFF;
	}

	for (i = 0; table[i].hardware_model != NULL; i++) {
		if (table[i].chip_id == cpid_match && table[i].board_id == bdid_match) {
			*device = &table[i];
			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_NO_DEVICE;
#endif
}

irecv_error_t irecv_devices_get_device_by_product_type(const char* product_type, irecv_device_t* device)
{
	int i = 0;
	irecv_device_t table = irecv_devicedb_get();

	if (!product_type || !device)
		return IRECV_E_INVALID_INPUT;

	*device = NULL;

	for (i = 0; table[i].product_type != NULL; i++) {
		if (!strcmp(product_type, table[i].product_type)) {
			*device = &table[i];
			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_NO_DEVICE;
}

irecv_error_t irecv_devices_get_device_by_hardware_model(const char* hardware_model, irecv_device_t* device)
{
	int i = 0;
	irecv_device_t table = irecv_devicedb_get();

	if (!hardware_model || !device)
		return IRECV_E_INVALID_INPUT;

	*device = NULL;

	for (i = 0; table[i].hardware_model != NULL; i++) {
		if (!strcasecmp(hardware_model, table[i].hardware_model)) {
			*device = &table[i];
			return IRECV_E_SUCCESS;
		}
	}

	return IRECV_E_NO_DEVICE;
}

irecv_client_t irecv_reconnect(irecv_client_t client, int initial_pause)
{
#ifdef USE_DUMMY
	return NULL;
#else
	irecv_error_t error = 0;
	irecv_client_t new_client = NULL;
	irecv_event_cb_t progress_callback = client->progress_callback;
	irecv_event_cb_t received_callback = client->received_callback;
	irecv_event_cb_t connected_callback = client->connected_callback;
	irecv_event_cb_t precommand_callback = client->precommand_callback;
	irecv_event_cb_t postcommand_callback = client->postcommand_callback;
	irecv_event_cb_t disconnected_callback = client->disconnected_callback;

	uint64_t ecid = client->device_info.ecid;

	if (check_context(client) == IRECV_E_SUCCESS) {
		irecv_cleanup(client);
	}

	if (initial_pause > 0) {
		debug("Waiting %d seconds for the device to pop up...\n", initial_pause);
		sleep(initial_pause);
	}

	error = irecv_open_with_ecid_and_attempts(&new_client, ecid, 10);
	if (error != IRECV_E_SUCCESS) {
		return NULL;
	}

	new_client->progress_callback = progress_callback;
	new_client->received_callback = received_callback;
	new_client->connected_callback = connected_callback;
	new_client->precommand_callback = precommand_callback;
	new_client->postcommand_callback = postcommand_callback;
	new_client->disconnected_callback = disconnected_callback;

	// keep old handle valid
	memcpy(client, new_client, sizeof(*client));
	free(new_client);
	new_client = client;

	if (new_client->connected_callback != NULL) {
		irecv_event_t event;
		event.size = 0;
		event.data = NULL;
		event.progress = 0;
		event.type = IRECV_CONNECTED;
		new_client->connected_callback(new_client, &event);
	}

	return new_client;
#endif
}
