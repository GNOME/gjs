#!/bin/sh

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$LOG_COMPILER $LOG_FLAGS $TOP_BUILDDIR/gjs-console"
else
    gjs="$LOG_COMPILER $LOG_FLAGS gjs-console"
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
    if test $exit_code -ne 0; then
        echo "ok $total - $1"
    else
        echo "not ok $total - $1"
    fi
}

skip () {
    total=$((total + 1))
    echo "ok $total - $1 # SKIP $2"
}
