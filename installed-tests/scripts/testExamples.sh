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
        echo "not ok $total - $1"
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

# Run the examples
$gjs examples/gio-cat.js Makefile
report "run the gio-cat.js example"

if [[ -n "${ENABLE_GTK}" ]]; then
    $gjs examples/calc.js _AUTO_QUIT
    report "run the calc.js example"

    $gjs examples/gtk.js _AUTO_QUIT
    report "run the gtk.js example"

    $gjs examples/gettext.js _AUTO_QUIT
    report "run the gettext.js example"

    $gjs examples/webkit.js _AUTO_QUIT
    report "run the webkit.js example"
fi
echo "1..$total"
