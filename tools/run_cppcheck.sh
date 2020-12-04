#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2017 Claudio André <claudioandre.br@gmail.com>

cd ${BUILDDIR:-_build}
if ! ninja -t compdb > compile_commands.json; then
    echo 'Generating compile_commands.json failed.'
    exit 1
fi
cppcheck --project=compile_commands.json --inline-suppr \
    --enable=warning,performance,portability,missingInclude \
    -UHAVE_PRINTF_ALTERNATIVE_INT \
    --library=gtk,../tools/cppcheck.cfg --force --quiet $@
