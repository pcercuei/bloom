#!/bin/bash

set -e

cd $(dirname $0)

BINUTILS_VERSION=2.42

if [ ! -f binutils-${BINUTILS_VERSION}.tar.xz ] ; then
	wget https://sourceware.org/pub/binutils/releases/binutils-${BINUTILS_VERSION}.tar.xz
fi

if [ ! -d binutils-${BINUTILS_VERSION} ] ; then
	tar xJf binutils-${BINUTILS_VERSION}.tar.xz
fi

cd binutils-${BINUTILS_VERSION}

cd libiberty
CC=kos-cc ./configure --host=sh-elf --prefix="" --enable-install-libiberty
DESTDIR=${KOS_PORTS} make install

cd ../libsframe
CC=kos-cc ./configure --host=sh-elf --prefix="" --enable-install-libbfd
DESTDIR=${KOS_PORTS} make install

cd ../bfd
CFLAGS="-Wno-incompatible-pointer-types" CC=kos-cc ./configure --host=sh-elf --prefix="" --enable-install-libbfd
DESTDIR=${KOS_PORTS} make install

cd ../opcodes
CC=kos-cc ./configure --host=sh-elf --prefix="" --enable-install-libbfd
DESTDIR=${KOS_PORTS} make install
