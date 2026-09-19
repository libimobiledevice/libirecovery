#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef USE_DUMMY
#ifndef _WIN32
#ifndef HAVE_IOKIT
#include <libusb.h>
#include "libirecovery-private.h"

int main(void)
{
	if (irecv_usb_should_clear_halt(LIBUSB_ERROR_TIMEOUT)) {
		return 1;
	}
	if (!irecv_usb_should_clear_halt(LIBUSB_ERROR_PIPE)) {
		return 1;
	}
	return 0;
}
#else
int main(void) { return 77; }
#endif
#else
int main(void) { return 77; }
#endif
#else
int main(void) { return 77; }
#endif
