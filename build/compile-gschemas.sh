#!/bin/sh

SCHEMADIR="$MESON_INSTALL_PREFIX/share/glib-2.0/schemas"

if test -z "$DESTDIR"; then
    echo "Compiling GSettings schemas..."
    glib-compile-schemas "$SCHEMADIR"
fi
