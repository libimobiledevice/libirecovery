# libirecovery

## About

libirecovery is a cross-platform library which implements communication to
iBoot/iBSS found on Apple's iOS devices via USB. A command-line utility is also
provided.

The software is completely open-source, the source code is released under the
terms of the LGPL 2.1. The full license text can be found in the LICENSE file.

This is a fork of an older version from http://www.openjailbreak.org/ and is
ment to be used with idevicerestore from the libimobiledevice project.

## Requirements

Development Packages of:
* libusb (Darwin: IOKit, Windows: SetupAPI)
* libreadline

Software:
* make
* autoheader
* automake
* autoconf
* libtool
* pkg-config
* gcc or clang

## Installation

To compile run:
```bash
./autogen.sh
make
sudo make install
```

## Who/What/Where?

* Home:	https://www.libimobiledevice.org/
* Code: `git clone https://git.libimobiledevice.org/libirecovery.git`
* Code (Mirror): `git clone https://github.com/libimobiledevice/libirecovery.git`
* Tickets: https://github.com/libimobiledevice/libirecovery/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* IRC: irc://irc.freenode.net#libimobiledevice
* Twitter: https://twitter.com/libimobiledev

## Credits

Apple, iPhone, iPod, and iPod Touch are trademarks of Apple Inc.
libirecovery is an independent software library and has not been authorized,
sponsored, or otherwise approved by Apple Inc.

README Updated on:
	2019-09-05
