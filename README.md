# libirecovery

*The libirecovery library allows communication with iBoot/iBSS of iOS devices
via USB.*

![](https://github.com/libimobiledevice/libirecovery/workflows/build/badge.svg)

## Features

libirecovery is a cross-platform library which implements communication to
iBoot/iBSS found on Apple's iOS devices via USB. A command-line utility named
`irecovery` is also provided.

This is a fork of an older version from former openjailbreak.org and is meant to
be used with [idevicerestore](https://github.com/libimobiledevice/idevicerestore.git/) from the [libimobiledevice](https://github.com/libimobiledevice/) project.

## Installation / Getting started

### Debian / Ubuntu Linux

First install all required dependencies and build tools:
```shell
sudo apt-get install \
	build-essential \
	pkg-config \	
	checkinstall \
	git \
	autoconf \
	automake \
	libtool-bin \
	libimobiledevice-glue-dev \
	libreadline-dev \
	libusb-1.0-0-dev
```

Then clone the actual project repository:
```shell
git clone https://github.com/libimobiledevice/libirecovery.git
cd libirecovery
```

Now you can build and install it:
```shell
./autogen.sh
make
sudo make install
```

## Usage

First of all attach your device to your machine. Make sure your device is not
in normal mode. You can use the `ideviceenterrecovery` application from
[libimobiledevice](https://github.com/libimobiledevice/libimobiledevice.git/)
to let your device boot into recovery mode if you need it.

Then simply run:
```shell
irecovery --shell
```

This connects to your device and opens a simple shell to interact with the
device.

For instance to make your device boot into normal mode again use:
```shell
setenv auto-boot true
saveenv
reboot
```

Please consult the usage information or manual page for a full documentation of
available command line options:
```shell
irecovery --help
man irecovery
```

## Contributing

We welcome contributions from anyone and are grateful for every pull request!

If you'd like to contribute, please fork the `master` branch, change, commit and
send a pull request for review. Once approved it can be merged into the main
code base.

If you plan to contribute larger changes or a major refactoring, please create a
ticket first to discuss the idea upfront to ensure less effort for everyone.

Please make sure your contribution adheres to:
* Try to follow the code style of the project
* Commit messages should describe the change well without being too short
* Try to split larger changes into individual commits of a common domain
* Use your real name and a valid email address for your commits

We are still working on the guidelines so bear with us!

## Links

* Homepage: https://libimobiledevice.org/
* Repository: https://git.libimobiledevice.org/libirecovery.git
* Repository (Mirror): https://github.com/libimobiledevice/libirecovery.git
* Issue Tracker: https://github.com/libimobiledevice/libirecovery/issues
* Mailing List: https://lists.libimobiledevice.org/mailman/listinfo/libimobiledevice-devel
* Twitter: https://twitter.com/libimobiledev

## License

This project is licensed under the [GNU Lesser General Public License v2.1](https://www.gnu.org/licenses/lgpl-2.1.en.html),
also included in the repository in the `COPYING` file.

## Credits

Apple, iPhone, iPad, iPod, iPod Touch, Apple TV, Apple Watch, Mac, iOS,
iPadOS, tvOS, watchOS, and macOS are trademarks of Apple Inc.

This project is an independent software library and has not been authorized,
sponsored, or otherwise approved by Apple Inc.

README Updated on: 2023-04-22
