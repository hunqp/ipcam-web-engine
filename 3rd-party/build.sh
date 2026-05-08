#!/bin/bash

# Force STOP if any error occurs
set -e 

# OPTIONS: 0 = native build (PC), 1 = cross compile
CROSS_BUILD=0

# SETUP: Toolchains and environment
if [ "$CROSS_BUILD" = "1" ]; then
    export TOOLCHAIN=/opt/arm-toolchain/bin
    export CROSS=arm-linux-gnueabihf
    export PATH="$TOOLCHAIN:$PATH"

    export CC=${CROSS}-gcc
    export CXX=${CROSS}-g++
    export AR=${CROSS}-ar
    export AS=${CROSS}-as
    export LD=${CROSS}-ld
    export STRIP=${CROSS}-strip
    export RANLIB=${CROSS}-ranlib

    HOST_OPT="--host=$CROSS"
else
    HOST_OPT=""
fi

# Prepare
if [ ! -f "lighttpd1.4-master.zip" ]; then
    echo "CAN NOT FIND lighttpd1.4-master.zip"
    exit 1
fi

rm -rf lighttpd1.4-master envir
unzip -o lighttpd1.4-master.zip
cd lighttpd1.4-master
./autogen.sh

# Build
./configure $HOST_OPT       \
    --prefix=/usr           \
    --without-bzip2         \
    --without-webdav-props  \
    --without-webdav-locks  \
    --without-lua           \
    --without-mysql         \
    --without-openssl

make -j$(nproc)

make DESTDIR="$PWD/../../envir" install

if [ "$CROSS_BUILD" = "1" ]; then
    $STRIP "$PWD/../../envir/usr/sbin/lighttpd" || true
fi