#!/bin/sh

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$TOP_BUILDDIR/gjs-console"
else
    gjs="gjs-console"
fi

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

"$gjs" -c 'imports.signals.addSignalMethods({connect: "foo"})' 2>&1 | \
    grep -q 'addSignalMethods is replacing existing .* connect method'
report "overwriting method with Signals.addSignalMethods() should warn"

echo "1..$total"
