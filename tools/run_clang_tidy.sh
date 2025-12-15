#!/bin/bash
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

if [ "$1" = '--help' ] || [ "$1" = '-h' ]; then
    echo "usage: $0 [ COMMIT ]"
    echo "Run clang-tidy on the GJS codebase."
    echo "If COMMIT is given, analyze only the changes since that commit,"
    echo "including uncommitted changes."
    echo "Otherwise, analyze all files."
    echo "See .clang-tidy for the list of checks."
    exit 0
fi

BUILD="${BUILDDIR:-_build}"
pushd "${BUILD}" || exit 1
if ! ninja -t compdb > compile_commands.json; then
    echo 'Generating compile_commands.json failed.'
    exit 1
fi
popd || exit 1

if [ $# -eq 0 ]; then
    run-clang-tidy -p "${BUILD}" gi/*.cpp gjs/*.cpp installed-tests/*.cpp \
        installed-tests/js/libgjstesttools/*.cpp libgjs-private/*.c \
        modules/*.cpp test/*.cpp util/*.cpp
else
    git diff -U0 --no-color "$1" | \
        /usr/share/clang/clang-tidy-diff.py -p1 -path "${BUILD}"
fi
