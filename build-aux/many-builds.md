# NAME

build-aux/many-builds.py - build steam-runtime-tools and pressure-vessel

# SYNOPSIS

**build-aux/many-builds.py** [*OPTIONS*] **deps**

**build-aux/many-builds.py** [*OPTIONS*] **setup** [*MESON SETUP OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **build** [*NINJA OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **install** [*NINJA OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **test** [*MESON TEST OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **clean** [*NINJA OPTIONS*]

# DESCRIPTION

`build-aux/many-builds.py` partially automates developer setup and
testing for steam-runtime-tools and pressure-vessel.

# REQUIREMENTS

You'll need about 25G of space available in your home directory
for container images and so on
(assuming **--podman**;
avoid using that option if you are space-constrained).
On a filesystem with transparent compression,
such as btrfs with `compress=zstd`,
this can be reduced to about 15G.

On x86, 32-bit support is required.
On Debian and derivatives,
this means i386 as a foreign architecture:

    sudo dpkg --add-architecture i386
    sudo apt update

Using **build-aux/many-builds.py** requires these Debian packages,
or their closest equivalents on other distributions:

* bubblewrap
* clang
* dbus-daemon
* docker.io (if using **--docker**)
* git
* glslang-tools
* gobject-introspection
* gtk-doc-tools
* libc6:i386
* libcap-dev (not to be confused with libcap-ng)
* libelf-dev (from elfutils, not to be confused with the older libelfg0-dev)
* libgl-dev
* libglib2.0-dev
* libjson-glib-dev
* libva-dev
* libvulkan-dev
* libwaffle-dev
* libx11-dev
* libxau-dev
* libxcb1-dev
* libxrandr-dev
* meson
* pandoc
* pkgconf (or pkg-config in older releases)
* podman (if using **--podman**)
* python3
* python3-vdf

For 32-bit builds on x86,
also install:

* lib32gcc-$(gcc -dumpversion)-dev

By default (when not using the **--docker** or **--podman** options),
`many-builds.py` also has
[the same user namespace requirements as Flatpak](https://github.com/flatpak/flatpak/wiki/User-namespace-requirements).
The same requirements apply to running tests for pressure-vessel.

When using **--podman**,
instead **podman**(1) must be installed.

## Optional packages

These packages add functionality but are not strictly required:

* gcovr and/or lcov
* libdbus-1-dev
* libfontconfig-dev
* libsdl2-dev
* libsdl2-ttf-dev

## Cross-compiling

For aarch64 builds (optional),
also run:

    sudo dpkg --add-architecture arm64
    sudo apt update

and install:

* gcc-aarch64-linux-gnu

plus arm64 versions of:

* libcap-dev
* libelf-dev
* libgl-dev
* libglib2.0-dev
* libjson-glib-dev
* libva-dev
* libvulkan-dev
* libwaffle-dev
* libx11-dev
* libxau-dev
* libxcb1-dev
* libxrandr-dev
* pkgconf

Then re-run `build-aux/many-builds.py [OPTIONS] setup`.

This will enable building `_build/arm64`.

Similarly,
for `_build/i386` install i386 versions of the packages above.
`_build/i386` uses `-m32`,
so you do not strictly need `gcc-i686-linux-gnu`.

# OPTIONS

**--srcdir** *PATH*
:   Relative or absolute path to the `steam-runtime-tools` source
    directory. The default is the current working directory.

**--builddir-parent** *PATH*
:   Relative or absolute path to the parent directory for all builds.
    The default is `_build`. Individual builds and additional required
    files will appear in subdirectories such as `_build/host` and
    `_build/containers`.

    Instead of using this option, it is usually more convenient to make
    a symbolic link such as `_build -> ../builds/steam-runtime-tools`
    if a separate build location is desired, and then use the default.

**--docker**
:   Use **docker**(1) to do builds. The default is to use **bwrap**(1).
    Building with Docker requires that the invoking user is either in
    the `docker` Unix group or able to run **sudo**(8), either of which
    is equivalent to full root privileges, so this mode cannot be used on
    machines where gaining full root privileges would be unacceptable.

**--podman**
:   Use **podman**(1) to do builds. The default is to use **bwrap**(1).
    Building with Podman requires newuidmap, newgidmap and a uid range
    configured for the current user in /etc/subuid and /etc/subgid:
    see [Troubleshooting](https://github.com/containers/podman/blob/main/troubleshooting.md)
    for details.

# STEPS

## deps

The **deps** step downloads
the necessary Steam Linux Runtime container runtime images to be able
to test **pressure-vessel-wrap**, and the necessary SDK image to be
able to compile for Steam Runtime 1 'scout'.

By default, the same SDK sysroot tarball that will be used for testing
is used for compilation. If the **--docker** or **--podman** options
are used, the official OCI container for the Steam Runtime 1 SDK will
be downloaded and used for compilation.

## setup

The **setup** step is similar to **meson setup**.

## build

The **build** step is similar to **meson compile**, but it only builds
a subset of the configurations from the **setup** step.

More specialized configurations such as *${builddir\_parent}*/**coverage**
can be compiled in the usual way with commands like
`meson compile -C _build/coverage` or `ninja -C _build/coverage`.

## install

The **install** step builds a complete relocatable version of the
Steam Runtime 1 'scout' builds of pressure-vessel. This can be
found in `_build/scout-relocatable`, and is also copied into
`_build/containers/pressure-vessel` for testing.

The other builds are not installed.

## test

The **test** step runs unit tests, with as comprehensive a test coverage
as possible. This will take a while.

## clean

The **clean** step is similar to **ninja -C ... clean**.

<!-- vim:set sw=4 sts=4 et: -->
