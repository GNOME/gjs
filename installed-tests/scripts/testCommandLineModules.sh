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

cat <<EOF >dynamicImplicitMainloopImportee.js
export const EXIT_CODE = 21;
EOF

cat <<EOF >dynamicImplicitMainloop.js
import("./dynamicImplicitMainloopImportee.js")
    .then(({ EXIT_CODE }) => {
      imports.system.exit(EXIT_CODE);
    });
EOF

$gjs dynamicImplicitMainloop.js
test $? -eq 21
report "ensure dynamic imports resolve without an explicit mainloop"

cat <<EOF >dynamicTopLevelAwaitImportee.js
export const EXIT_CODE = 32;
EOF

cat <<EOF >dynamicTopLevelAwait.js
const {EXIT_CODE} = await import("./dynamicTopLevelAwaitImportee.js")
const system = await import('system');

system.exit(EXIT_CODE);
EOF

$gjs -m dynamicTopLevelAwait.js
test $? -eq 32
report "ensure top level await can import modules"


rm -f doubledynamic.js doubledynamicImportee.js \
      dynamicImplicitMainloop.js dynamicImplicitMainloopImportee.js \
      dynamicTopLevelAwait.js dynamicTopLevelAwaitImportee.js

echo "1..$total"
