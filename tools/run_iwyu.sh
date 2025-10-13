#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

SRCDIR=$(pwd)

if [ "$1" = '--help' -o "$1" = '-h' ]; then
    echo "usage: $0 [ COMMIT ]"
    echo "Run include-what-you-use on the GJS codebase."
    echo "If COMMIT is given, analyze only the files changed since that commit,"
    echo "including uncommitted changes."
    echo "Otherwise, analyze all files."
    exit 0
fi

if [ $# -eq 0 ]; then
    files=all
else
    # make stat changes not show up as modifications
    git update-index -q --really-refresh

    files="$(git diff-tree --name-only -r $1..) $(git diff-index --name-only HEAD)"
fi

should_analyze () {
    file=$(realpath --relative-to=$SRCDIR $1)
    case "$files" in
        all) return 0 ;;
        *$file*) return 0 ;;
        *${file%.cpp}.h*) return 0 ;;
        *${file%.cpp}-inl.h*) return 0 ;;
        *${file%.cpp}-private.h*) return 0 ;;
        *${file%.c}.h*) return 0 ;;
        *) return 1 ;;
    esac
}

cd ${BUILDDIR:-_build}
if ! ninja -t compdb > compile_commands.json; then
    echo 'Generating compile_commands.json failed.'
    exit 1
fi
# Temporary, while building GLib
if ! ninja subprojects/glib/glib/glib-visibility.h subprojects/glib/girepository/gi-visibility.h subprojects/glib/gio/gio-visibility.h; then
    echo 'Generating header files failed.'
    exit 1
fi

echo "files: $files"

IWYU="python3 $(which iwyu_tool iwyu-tool iwyu_tool.py 2>/dev/null) -p ."
IWYU_FEDORA_BUG_ARGS="-I/usr/lib/clang/20/include"
IWYU_TOOL_ARGS="-I../gjs $IWYU_FEDORA_BUG_ARGS"
IWYU_ARGS="-Wno-pragma-once-outside-header"
IWYU_RAW="include-what-you-use -xc++ -std=c++17 -Xiwyu --keep=config.h $IWYU_ARGS"
IWYU_RAW_INC="-I. -I.. $(pkg-config --cflags girepository-2.0 mozjs-140) $IWYU_FEDORA_BUG_ARGS"
PRIVATE_MAPPING="-Xiwyu --mapping_file=$SRCDIR/tools/gjs-private-iwyu.imp -Xiwyu --keep=config.h"
PUBLIC_MAPPING="-Xiwyu --mapping_file=$SRCDIR/tools/gjs-public-iwyu.imp"
POSTPROCESS="python3 $SRCDIR/tools/process_iwyu.py"
EXIT=0

# inline-only files with no implementation file don't appear in the compilation
# database, so iwyu_tool cannot process them
for FILE in $SRCDIR/gi/arg-types-inl.h $SRCDIR/gi/js-value-inl.h \
    $SRCDIR/gi/info.h $SRCDIR/gi/utils-inl.h $SRCDIR/gjs/auto.h \
    $SRCDIR/gjs/enum-utils.h $SRCDIR/gjs/gerror-result.h \
    $SRCDIR/gjs/jsapi-util-args.h $SRCDIR/gjs/jsapi-util-root.h \
    $SRCDIR/modules/cairo-module.h
do
    if should_analyze $FILE; then
        if ! $IWYU_RAW $PRIVATE_MAPPING $(realpath --relative-to=. $FILE) \
            $IWYU_RAW_INC 2>&1 | $POSTPROCESS; then
            EXIT=1
        fi
    fi
done

for FILE in $SRCDIR/gi/*.cpp $SRCDIR/gjs/*.cpp $SRCDIR/modules/*.cpp \
    $SRCDIR/test/*.cpp $SRCDIR/util/*.cpp $SRCDIR/libgjs-private/*.c
do
    test $FILE = $SRCDIR/gjs/console.cpp && continue
    if should_analyze $FILE; then
        if ! $IWYU $FILE -- $PRIVATE_MAPPING $IWYU_TOOL_ARGS | $POSTPROCESS; then
            EXIT=1
        fi
    fi
done

for FILE in $SRCDIR/gjs/console.cpp $SRCDIR/installed-tests/minijasmine.cpp
do
    if should_analyze $FILE; then
        if ! $IWYU $FILE -- $PUBLIC_MAPPING $IWYU_TOOL_ARGS | $POSTPROCESS; then
            EXIT=1
        fi
    fi
done

if test $EXIT -eq 0; then echo "No changes needed."; fi
exit $EXIT
