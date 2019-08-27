#!/bin/sh

ninja -C _build
cd _build

IWYU="iwyu_tool -p ."
PRIVATE_MAPPING="--mapping_file=../tools/gjs-private-iwyu.imp"
PUBLIC_MAPPING="--mapping_file=../tools/gjs-public-iwyu.imp"

for FILE in ../gi/*.cpp ../gi/gjs_gi_trace.h ../gjs/atoms.cpp \
    ../gjs/byteArray.cpp ../gjs/coverage.cpp ../gjs/debugger.cpp \
    ../gjs/deprecation.cpp ../gjs/error-types.cpp ../gjs/engine.cpp \
    ../gjs/global.cpp ../gjs/importer.cpp ../gjs/jsapi-util-args.h \
    ../gjs/jsapi-util-error.cpp ../gjs/jsapi-util-root.h \
    ../gjs/jsapi-util-string.cpp ../js/jsapi-util.cpp ../gjs/module.cpp \
    ../gjs/native.cpp ../gjs/stack.cpp ../modules/cairo-*.cpp \
    ../modules/console.cpp ../modules/system.cpp ../test/*.cpp ../util/*.cpp \
    ../libgjs-private/*.c
do
    $IWYU $FILE -- $PRIVATE_MAPPING
done
$IWYU ../gjs/context.cpp -- $PRIVATE_MAPPING \
    --check_also=../gjs/context-private.h
$IWYU ../gjs/jsapi-dynamic-class.cpp -- $PRIVATE_MAPPING \
    --check_also=../gjs/jsapi-class.h
$IWYU ../gjs/mem.cpp -- $PRIVATE_MAPPING --check_also=../gjs/mem-private.h
$IWYU ../gjs/profiler.cpp -- $PRIVATE_MAPPING \
    --check_also=../gjs/profiler-private.h
$IWYU ../modules/cairo.cpp -- $PRIVATE_MAPPING \
    --check_also=../modules/cairo-module.h \
    --check_also=../modules/cairo-private.h

for FILE in ../gjs/macros.h ../gjs/console.cpp \
    ../installed-tests/minijasmine.cpp
do
    $IWYU $FILE -- $PUBLIC_MAPPING
done
