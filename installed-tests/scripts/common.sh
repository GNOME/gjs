#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2016 Philip Chimento <philip.chimento@gmail.com>

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$TOP_BUILDDIR/gjs-console"
    fixtures="$TOP_SRCDIR/installed-tests/scripts"
else
    # shellcheck disable=SC2034
    gjs="gjs-console"
    # shellcheck disable=SC2034
    fixtures="$(cd "$(dirname "$0")" && pwd)"
fi

# Avoid interference in the profiler tests from stray environment variable
unset GJS_ENABLE_PROFILER

total=0

report () {
    exit_code=$?
    total=$((total + 1))
    if test $exit_code -eq 0; then
        echo "ok $total - $1"
    else
        echo "not ok $total - $1 [EXIT CODE: $exit_code]"
    fi
}

report_timeout () {
    exit_code=$?
    total=$((total + 1))
    if test $exit_code -eq 0 -o $exit_code -eq 124; then
        echo "ok $total - $1"
    else
        echo "not ok $total - $1 [EXIT CODE: $exit_code]"
    fi
}

report_xfail () {
    exit_code=$?
    total=$((total + 1))
    if test $exit_code -eq 23; then
        echo "not ok $total - $1 (leaked memory)"
    elif test $exit_code -ne 0; then
        echo "ok $total - $1 [EXIT CODE: $exit_code]"
    else
        echo "not ok $total - $1"
    fi
}

skip () {
    total=$((total + 1))
    echo "ok $total - $1 # SKIP $2"
}
