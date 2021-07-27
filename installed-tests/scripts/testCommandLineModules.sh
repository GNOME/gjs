#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2016 Endless Mobile, Inc.
# SPDX-FileCopyrightText: 2016 Philip Chimento <philip.chimento@gmail.com>

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

# Avoid interference in the profiler tests from stray environment variable
unset GJS_ENABLE_PROFILER

# Avoid interference in the warning tests from G_DEBUG=fatal-warnings/criticals
OLD_G_DEBUG="$G_DEBUG"

cat <<EOF >doubledynamicImportee.js
export function noop() {}
EOF

# this JS script should succeed without an error on the second import
cat <<EOF >doubledynamic.js
let done = false;

import("./doubledynamicImportee.js")
    .then(ddi => {
        ddi.noop();
    })
    .finally(() => {
        if (done)
            imports.mainloop.quit();
        done = true;
    });

import("./doubledynamicImportee.js")
    .then(ddi => {
        ddi.noop();
    })
    .finally(() => {
        if (done)
            imports.mainloop.quit();
        done = true;
    });

imports.mainloop.run();
EOF

$gjs doubledynamic.js
report "ensure dynamic imports load even if the same import resolves elsewhere first"

rm -f doubledynamic.js doubledynamicImportee.js

echo "1..$total"
