#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$TOP_BUILDDIR/gjs-console"
else
    gjs=gjs-console
fi

echo 1..1

DEBUGGER_SCRIPT="$1"
JS_SCRIPT="$1.js"
EXPECTED_OUTPUT="$1.output"
THE_DIFF=$("$gjs" -d "$JS_SCRIPT" < "$DEBUGGER_SCRIPT" | sed \
    -e "s#$1#$(basename $1)#g" \
    -e "s/0x[0-9a-f]\{4,16\}/0xADDR/g" \
    | diff -u "$EXPECTED_OUTPUT" -)
EXITCODE=$?

if test -n "$THE_DIFF"; then
    echo "not ok 1 - $1"
    echo "$THE_DIFF" | while read line; do echo "#$line"; done
else
    if test $EXITCODE -ne 0; then
        echo "not ok 1 - $1  # command failed"
        exit 1
    fi
    echo "ok 1 - $1"
fi
