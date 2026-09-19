/*
 * Private helpers shared with focused transport tests.
 */
#ifndef LIBIRECOVERY_PRIVATE_H
#define LIBIRECOVERY_PRIVATE_H

#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
static inline int irecv_usb_should_clear_halt(int transfer_result)
{
	return transfer_result == LIBUSB_ERROR_PIPE;
}
#endif
#endif
#endif

#endif
