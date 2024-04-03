#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2017 Claudio André <claudioandre.br@gmail.com>
# SPDX-FileCopyrightText: 2021 Philip Chimento <philip.chimento@gmail.com>

cd ${BUILDDIR:-_build}
if ! test -f compile_commands.json; then
    echo "compile_commands.json missing. Generate it with ninja -t compdb"
    exit 1
fi

# Usage:
# add -q for just the errors and no progress reporting.
# add -f to force-check every configuration (takes a long time).
# add -j4 for faster execution with multiple jobs.
# add --enable=style to check style rules. There are some false positives.

# duplInheritedMember: does not mix well with overshadowing constexpr static
# members in CRTP classes.
# incorrectStringBooleanError: does not mix well with the assertion message
# idiom.
# nullPointerRedundantCheck, nullPointerArithmeticRedundantCheck: False positive
# when using g_assert_nonnull(). Check again when
# https://github.com/danmar/cppcheck/pull/5830 is available.
cppcheck --project=compile_commands.json --check-level=exhaustive \
    --inline-suppr --error-exitcode=1 --enable=warning,performance,portability \
    --suppress=duplInheritedMember --suppress=incorrectStringBooleanError \
    --suppress=nullPointerArithmeticRedundantCheck \
    --suppress=nullPointerRedundantCheck \
    --suppress=*:subprojects/* --suppress=*:js-resources.c \
    --suppress=*:test/mock-js-resources.c \
    --suppress=*:installed-tests/js/jsunit-resources.c \
    --library=gtk,cairo,posix,../tools/cppcheck.cfg $@
