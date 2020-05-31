#!/bin/sh

SRCDIR=$(pwd)

if [ "$1" == '--help' -o "$1" == '-h' ]; then
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
    files="$(git diff-tree --name-only -r $1..) $(git diff-index --name-only HEAD)"
fi

echo "files: $files"

should_analyze () {
    file=$(realpath --relative-to=$SRCDIR $1)
    case "$files" in
        all) return 0 ;;
        *$file*) return 0 ;;
        *${file%.cpp}.h*) return 0 ;;
        *${file%.cpp}-private.h*) return 0 ;;
        *${file%.c}.h*) return 0 ;;
        *) return 1 ;;
    esac
}

cd ${BUILDDIR:-_build}
if ! ninja; then
    echo 'Build failed.'
    exit 1
fi

IWYU="iwyu_tool -p ."
PRIVATE_MAPPING="-Xiwyu --mapping_file=$SRCDIR/tools/gjs-private-iwyu.imp -Xiwyu --keep=config.h"
PUBLIC_MAPPING="-Xiwyu --mapping_file=$SRCDIR/tools/gjs-public-iwyu.imp"
POSTPROCESS="python3 $SRCDIR/tools/process_iwyu.py"

for FILE in $SRCDIR/gi/*.cpp $SRCDIR/gjs/atoms.cpp $SRCDIR/gjs/byteArray.cpp \
    $SRCDIR/gjs/coverage.cpp $SRCDIR/gjs/debugger.cpp \
    $SRCDIR/gjs/deprecation.cpp $SRCDIR/gjs/error-types.cpp \
    $SRCDIR/gjs/engine.cpp $SRCDIR/gjs/global.cpp $SRCDIR/gjs/importer.cpp \
    $SRCDIR/gjs/jsapi-util-error.cpp $SRCDIR/gjs/jsapi-util-string.cpp \
    $SRCDIR/gjs/module.cpp $SRCDIR/gjs/native.cpp $SRCDIR/gjs/stack.cpp \
    $SRCDIR/modules/cairo-*.cpp $SRCDIR/modules/console.cpp \
    $SRCDIR/modules/system.cpp $SRCDIR/test/*.cpp $SRCDIR/util/*.cpp \
    $SRCDIR/libgjs-private/*.c
do
    should_analyze $FILE && $IWYU $FILE -- $PRIVATE_MAPPING | $POSTPROCESS
done

should_analyze $SRCDIR/gjs/context.cpp && \
$IWYU $SRCDIR/gjs/context.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=*/gjs/context-private.h | $POSTPROCESS

( should_analyze $SRCDIR/gjs/jsapi-dynamic-class.cpp || \
    should_analyze $SRCDIR/gjs/jsapi-class.h ) && \
$IWYU $SRCDIR/gjs/jsapi-dynamic-class.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=*/gjs/jsapi-class.h | $POSTPROCESS

( should_analyze $SRCDIR/gjs/jsapi-util.cpp ||
    should_analyze $SRCDIR/gjs/jsapi-util-args.h || \
    should_analyze $SRCDIR/gjs/jsapi-util-root.h ) && \
$IWYU $SRCDIR/gjs/jsapi-util.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=*/gjs/jsapi-util-args.h \
    -Xiwyu --check_also=*/gjs/jsapi-util-root.h | $POSTPROCESS

should_analyze $SRCDIR/gjs/mem.cpp && \
$IWYU $SRCDIR/gjs/mem.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=*/gjs/mem-private.h | $POSTPROCESS

should_analyze $SRCDIR/gjs/profiler.cpp && \
$IWYU $SRCDIR/gjs/profiler.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=*/gjs/profiler-private.h | $POSTPROCESS

( should_analyze $SRCDIR/modules/cairo.cpp ||
    should_analyze $SRCDIR/modules/cairo-module.h ) && \
$IWYU $SRCDIR/modules/cairo.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=*/modules/cairo-module.h \
    -Xiwyu --check_also=*/modules/cairo-private.h | $POSTPROCESS

for FILE in $SRCDIR/gjs/console.cpp $SRCDIR/installed-tests/minijasmine.cpp
do
    should_analyze $FILE && $IWYU $FILE -- $PUBLIC_MAPPING | $POSTPROCESS
done
