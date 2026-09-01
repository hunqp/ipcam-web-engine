#!/bin/sh

if [ -z "$ENVIR_DIR" ]; then
    export ENVIR_DIR=$PWD
fi

killall -9 FastCGI
"$ENVIR_DIR/bin/lighttpd" -f "$ENVIR_DIR/etc/lighttpd.conf" -m "$ENVIR_DIR/lib"  -D
