#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Gary Li <gary.li1@uwaterloo.ca>

DIR="$( cd "$( dirname "${0}" )" && pwd )"
# shellcheck source=installed-tests/scripts/common.sh
. "${DIR}"/common.sh

if test "$CI_JOB_NAME" = "valgrind"; then
    skip "Issue 443 GObject wrapper disposed warning" "Valgrind takes too long on CI"
else
    $gjs -m "$fixtures/gcWrapperWarning.js" 2>&1 | \
        grep -q 'Wrapper for GObject.*was disposed, cannot set property string-object'
    report "Issue 443 GObject wrapper disposed warning"
fi

echo "1..$total"
