#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

BUILDROOT_DIR=$PWD
STAGING_DIR="${BUILDROOT_DIR}/output/staging"
PKG_CONFIG_DIR="${STAGING_DIR}/usr/lib/pkgconfig:${STAGING_DIR}/usr/share/pkgconfig"
OPENSSL_DIR="${OPENSSL_DIR:-${BUILDROOT_DIR}/libopenssl-1.1.1h}"
INSTALL_DIR="${SCRIPT_DIR}/envir"

if [ ! -d "${OPENSSL_DIR}/include/openssl" ] || [ ! -d "${OPENSSL_DIR}/lib" ]; then
    echo "CAN NOT FIND OpenSSL 1.1.1h under ${OPENSSL_DIR}"
    echo "Set OPENSSL_DIR=/path/to/openssl-1.1.1h if it is stored elsewhere."
    exit 1
fi

if [ -z "$CROSS_COMPILER" ]; then
    HOST_OPT=""
else
    export CC="${CROSS_COMPILER}-gcc"
    export CXX="${CROSS_COMPILER}-g++"
    export AR="${CROSS_COMPILER}-ar"
    export AS="${CROSS_COMPILER}-as"
    export LD="${CROSS_COMPILER}-ld"
    export STRIP="${CROSS_COMPILER}-strip"
    export RANLIB="${CROSS_COMPILER}-ranlib"

    HOST_OPT="--host=$(basename "$CROSS_COMPILER")"

    # Force pkg-config to find target libraries only
    export PKG_CONFIG_SYSROOT_DIR="${STAGING_DIR}"
    export PKG_CONFIG_LIBDIR="${PKG_CONFIG_DIR}"
    export CPPFLAGS="-I${STAGING_DIR}/usr/include"
    export LDFLAGS="-L${STAGING_DIR}/usr/lib"
fi

if [ ! -f "lighttpd1.4-master.zip" ]; then
    echo "CAN NOT FIND lighttpd1.4-master.zip"
    exit 1
fi

rm -rf lighttpd1.4-master

unzip -o lighttpd1.4-master.zip
cd lighttpd1.4-master

./autogen.sh

./configure ${HOST_OPT}        \
    --prefix=/usr/local        \
    --without-bzip2            \
    --without-webdav-props     \
    --without-webdav-locks     \
    --without-zlib             \
    --without-pcre             \
    --without-pcre2            \
    --without-lua              \
    --without-mysql            \
    --with-openssl="${OPENSSL_DIR}"

make -j"$(nproc)"

mkdir -p "${INSTALL_DIR}"

make DESTDIR="${INSTALL_DIR}" install

mkdir -p "${INSTALL_DIR}/bin"
mkdir -p "${INSTALL_DIR}/lib"

cp -f \
    "${INSTALL_DIR}/usr/local/sbin/lighttpd" \
    "${INSTALL_DIR}/bin/lighttpd"

# Copy Lighttpd modules, including mod_openssl.so
if [ -d "${INSTALL_DIR}/usr/local/lib" ]; then
    cp -a "${INSTALL_DIR}/usr/local/lib/." "${INSTALL_DIR}/lib/"
fi

# Ship the OpenSSL runtime used to build mod_openssl.so.
cp -a "${OPENSSL_DIR}"/lib/libssl.so* "${INSTALL_DIR}/lib/"
cp -a "${OPENSSL_DIR}"/lib/libcrypto.so* "${INSTALL_DIR}/lib/"

"${STRIP:-strip}" "${INSTALL_DIR}/bin/lighttpd" || true

find "${INSTALL_DIR}/lib" -name '*.so' -exec "${STRIP:-strip}" --strip-unneeded {} \; 2>/dev/null || true

echo "Lighttpd build completed"
