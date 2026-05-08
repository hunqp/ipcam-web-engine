#!/bin/bash

if [ -z "$ENVIR_DIR" ]; then
    echo "You MUST define ENVIR_DIR"
    exit 1
fi

"$ENVIR_DIR/bin/lighttpd" -f "$ENVIR_DIR/etc/lighttpd.conf" -D