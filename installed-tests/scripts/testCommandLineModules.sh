#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

DIR="$( cd "$( dirname "${0}" )" && pwd )"
# shellcheck source=installed-tests/scripts/common.sh
. "${DIR}"/common.sh

$gjs "$fixtures/doubledynamic.js"
report "ensure dynamic imports load even if the same import resolves elsewhere first"

$gjs "$fixtures/dynamicImplicitMainloop.js"
test $? -eq 21
report "ensure dynamic imports resolve without an explicit mainloop"

$gjs -m "$fixtures/dynamicTopLevelAwait.js"
test $? -eq 32
report "ensure top level await can import modules"

echo "1..$total"
