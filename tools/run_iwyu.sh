#!/bin/sh

SRCDIR=$(pwd)

cd ${BUILDDIR:-_build}
ninja

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
    $IWYU $FILE -- $PRIVATE_MAPPING | $POSTPROCESS
done
$IWYU $SRCDIR/gjs/context.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=$SRCDIR/gjs/context-private.h | $POSTPROCESS
$IWYU $SRCDIR/gjs/jsapi-dynamic-class.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=$SRCDIR/gjs/jsapi-class.h | $POSTPROCESS
$IWYU $SRCDIR/gjs/jsapi-util.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=$SRCDIR/gjs/jsapi-util-args.h \
    -Xiwyu --check_also=$SRCDIR/gjs/jsapi-util-root.h | $POSTPROCESS
$IWYU $SRCDIR/gjs/mem.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=$SRCDIR/gjs/mem-private.h | $POSTPROCESS
$IWYU $SRCDIR/gjs/profiler.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=$SRCDIR/gjs/profiler-private.h | $POSTPROCESS
$IWYU $SRCDIR/modules/cairo.cpp -- $PRIVATE_MAPPING \
    -Xiwyu --check_also=$SRCDIR/modules/cairo-module.h \
    -Xiwyu --check_also=$SRCDIR/modules/cairo-private.h | $POSTPROCESS

for FILE in $SRCDIR/gjs/console.cpp $SRCDIR/installed-tests/minijasmine.cpp
do
    $IWYU $FILE -- $PUBLIC_MAPPING | $POSTPROCESS
done
