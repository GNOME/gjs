#!/bin/bash
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

CHECKS_OVERRIDE=()

for arg in "$@"; do
    case $arg in
        --analyzer)
            CHECKS_OVERRIDE=('-checks=-*,clang-analyzer-*')
            shift
            ;;
        -h|--help)
            echo "usage: $0 [ --analyzer ] [ COMMIT ]"
            echo "Run clang-tidy on the GJS codebase."
            echo "If COMMIT is given, analyze only the changes since that"
            echo "commit, including uncommitted changes."
            echo "Otherwise, analyze all files."
            echo "See .clang-tidy for the list of checks."
            echo "--analyzer runs clang-static-analyzer checks instead of the"
            echo "checks listed in .clang-tidy."
            exit 0
            ;;
        *)
            ;;
    esac
done

BUILD="${BUILDDIR:-_build}"
pushd "${BUILD}" > /dev/null || exit 1
if ! ninja -t compdb > compile_commands.json; then
    echo 'Generating compile_commands.json failed.'
    exit 1
fi
popd > /dev/null || exit 1

if [ $# -eq 0 ]; then
    run-clang-tidy -p "${BUILD}" "${CHECKS_OVERRIDE[@]}" gi/*.cpp gjs/*.cpp \
        installed-tests/*.cpp installed-tests/js/libgjstesttools/*.cpp \
        libgjs-private/*.c modules/*.cpp test/*.cpp util/*.cpp
else
    git diff --stat "$1"
    git diff -U0 --no-color "$1" | \
        /usr/share/clang/clang-tidy-diff.py -p1 -path "${BUILD}" \
            "${CHECKS_OVERRIDE[@]}"
fi
