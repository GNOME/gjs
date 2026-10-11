#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

DIR="$( cd "$( dirname "${0}" )" && pwd )"
# shellcheck source=installed-tests/scripts/common.sh
. "${DIR}"/common.sh

$gjs -c 'imports.signals.addSignalMethods({connect: "foo"})' 2>&1 | \
    grep -q 'addSignalMethods is replacing existing .* connect method'
report "overwriting method with Signals.addSignalMethods() should warn"

$gjs -c 'imports.gi.GLib.get_home_dir("foobar")' 2>&1 | \
    grep -q 'Too many arguments to .*: expected 0, got 1'
report "passing too many arguments to a GI function should warn"

$gjs -c '**' 2>&1 | \
    grep -q 'SyntaxError.*@ <command line>:1:1'
report "file and line number are logged for syntax errors"

echo "1..$total"
