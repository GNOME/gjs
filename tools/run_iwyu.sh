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

echo "files: $files"

IWYU="python3 $(which iwyu_tool iwyu-tool iwyu_tool.py 2>/dev/null) -p ."
IWYU_TOOL_ARGS="-I../gjs"
IWYU_ARGS="-Wno-pragma-once-outside-header"
IWYU_RAW="include-what-you-use -xc++ -std=c++17 -Xiwyu --keep=config.h $IWYU_ARGS"
IWYU_RAW_INC="-I. -I.. $(pkg-config --cflags gobject-introspection-1.0 mozjs-128)"
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

for FILE in $SRCDIR/gi/*.cpp $SRCDIR/gjs/atoms.cpp $SRCDIR/gjs/byteArray.cpp \
    $SRCDIR/gjs/coverage.cpp $SRCDIR/gjs/debugger.cpp \
    $SRCDIR/gjs/deprecation.cpp $SRCDIR/gjs/engine.cpp \
    $SRCDIR/gjs/error-types.cpp $SRCDIR/gjs/global.cpp \
    $SRCDIR/gjs/internal.cpp $SRCDIR/gjs/importer.cpp \
    $SRCDIR/gjs/jsapi-util*.cpp $SRCDIR/gjs/mainloop.cpp \
    $SRCDIR/gjs/module.cpp $SRCDIR/gjs/native.cpp \
    $SRCDIR/gjs/objectbox.cpp $SRCDIR/gjs/promise.cpp $SRCDIR/gjs/stack.cpp \
    $SRCDIR/gjs/text-encoding.cpp $SRCDIR/modules/cairo-*.cpp \
    $SRCDIR/modules/console.cpp $SRCDIR/modules/print.cpp \
    $SRCDIR/modules/system.cpp $SRCDIR/test/*.cpp $SRCDIR/util/*.cpp \
    $SRCDIR/libgjs-private/*.c
do
    if should_analyze $FILE; then
        if ! $IWYU $FILE -- $PRIVATE_MAPPING $IWYU_TOOL_ARGS | $POSTPROCESS; then
            EXIT=1
        fi
    fi
done

# this header file is named differently from its corresponding implementation
if ( should_analyze $SRCDIR/gjs/jsapi-dynamic-class.cpp || \
    should_analyze $SRCDIR/gjs/jsapi-class.h ); then
    if ! $IWYU $SRCDIR/gjs/jsapi-dynamic-class.cpp -- $PRIVATE_MAPPING \
        $IWYU_TOOL_ARGS \
        -Xiwyu --check_also=*/gjs/jsapi-class.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

# include header files with private implementation along with their main files
for STEM in gjs/context gjs/mem gjs/profiler modules/cairo; do
    if should_analyze $SRCDIR/$STEM.cpp; then
        if ! $IWYU $SRCDIR/$STEM.cpp -- $PRIVATE_MAPPING $IWYU_TOOL_ARGS \
            -Xiwyu --check_also=*/$STEM-private.h | $POSTPROCESS; then
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
