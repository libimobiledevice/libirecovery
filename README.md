# libirecovery

*The libirecovery library allows communication with iBoot/iBSS of iOS devices
via USB.*

![](https://github.com/libimobiledevice/libirecovery/workflows/build/badge.svg)

## Table of Contents
- [Features](#features)
- [Building](#building)
  - [Prerequisites](#prerequisites)
    - [Linux (Debian/Ubuntu based)](#linux-debianubuntu-based)
    - [macOS](#macos)
    - [Windows](#windows)
  - [Configuring the source tree](#configuring-the-source-tree)
  - [Building and installation](#building-and-installation)
- [Usage](#usage)
- [Contributing](#contributing)
- [Links](#links)
- [License](#license)
- [Credits](#credits)

## Features

libirecovery is a cross-platform library which implements communication to
iBoot/iBSS found on Apple's iOS devices via USB. A command-line utility named
`irecovery` is also provided.

This is a fork of an older version from former openjailbreak.org and is meant to
be used with [idevicerestore](https://github.com/libimobiledevice/idevicerestore.git/) from the [libimobiledevice](https://github.com/libimobiledevice/) project.

## Building

### Prerequisites

You need to have a working compiler (gcc/clang) and development environent
available. This project uses autotools for the build process, allowing to
have common build steps across different platforms.
Only the prerequisites differ and they are described in this section.

libirecovery requires [libimobiledevice-glue](https://github.com/libimobiledevice/libimobiledevice-glue).
Check the [Building](https://github.com/libimobiledevice/libimobiledevice-glue?tab=readme-ov-file#building)
section of the README on how to build it. Note that some platforms might have it as a package.

#### Linux (Debian/Ubuntu based)

* Install all required dependencies and build tools:
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

  In case libimobiledevice-glue-dev is not available, you can manually build and install it. See note above.

#### macOS

* Make sure the Xcode command line tools are installed. Then, use either [MacPorts](https://www.macports.org/)
  or [Homebrew](https://brew.sh/) to install `automake`, `autoconf`, `libtool`, etc.

  Using MacPorts:
  ```shell
  sudo port install libtool autoconf automake pkgconfig
  ```

  Using Homebrew:
  ```shell
  brew install libtool autoconf automake pkg-config
  ```

#### Windows

* Using [MSYS2](https://www.msys2.org/) is the official way of compiling this project on Windows. Download the MSYS2 installer
  and follow the installation steps.

  It is recommended to use the _MSYS2 MinGW 64-bit_ shell. Run it and make sure the required dependencies are installed:

  ```shell
  pacman -S base-devel \
  	git \
  	mingw-w64-x86_64-gcc \
  	make \
  	libtool \
  	autoconf \
  	automake-wrapper \
  	pkg-config
  ```
  NOTE: You can use a different shell and different compiler according to your needs. Adapt the above command accordingly.

### Configuring the source tree

You can build the source code from a git checkout, or from a `.tar.bz2` release tarball from [Releases](https://github.com/libimobiledevice/libirecovery/releases).
Before we can build it, the source tree has to be configured for building. The steps depend on where you got the source from.

Since libirecovery depends on other packages, you should set the pkg-config environment variable `PKG_CONFIG_PATH`
accordingly. Make sure to use a path with the same prefix as the dependencies. If they are installed in `/usr/local` you would do

```shell
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
```

* **From git**

  If you haven't done already, clone the actual project repository and change into the directory.
  ```shell
  git clone https://github.com/libimobiledevice/libirecovery
  cd libirecovery
  ```

  Configure the source tree for building:
  ```shell
  ./autogen.sh
  ```

* **From release tarball (.tar.bz2)**

  When using an official [release tarball](https://github.com/libimobiledevice/libirecovery/releases) (`libirecovery-x.y.z.tar.bz2`)
  the procedure is slightly different.

  Extract the tarball:
  ```shell
  tar xjf libirecovery-x.y.z.tar.bz2
  cd libirecovery-x.y.z
  ```

  Configure the source tree for building:
  ```shell
  ./configure
  ```

Both `./configure` and `./autogen.sh` (which generates and calls `configure`) accept a few options, for example `--prefix` to allow
building for a different target folder. You can simply pass them like this:

```shell
./autogen.sh --prefix=/usr/local
```
or
```shell
./configure --prefix=/usr/local
```

Once the command is successful, the last few lines of output will look like this:
```
[...]
config.status: creating config.h
config.status: executing depfiles commands
config.status: executing libtool commands

Configuration for libirecovery 1.2.0:
-------------------------------------------

  Install prefix: .........: /usr/local
  USB backend: ............: IOKit

  Now type 'make' to build libirecovery 1.2.0,
  and then 'make install' for installation.
```

### Building and installation

If you followed all the steps successfully, and `autogen.sh` or `configure` did not print any errors,
you are ready to build the project. This is simply done with

```shell
make
```

If no errors are emitted you are ready for installation. Depending on whether
the current user has permissions to write to the destination directory or not,
you would either run
```shell
make install
```
_OR_
```shell
sudo make install
```

If you are on Linux, you want to run `sudo ldconfig` after installation to
make sure the installed libraries are made available.

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

README Updated on: 2024-03-23
