#!/bin/bash

# run with 'tools/run_iwyu.sh 2> >(tee iwyu.log)'

: "${BUILDDIR:=$HOME/.cache/jhbuild/build/gjs}"

PRIVATE_MAPPING="-Xiwyu --mapping_file=tools/gjs-private-iwyu.imp"
PUBLIC_MAPPING="-Xiwyu --mapping_file=tools/gjs-public-iwyu.imp"

LIBS_INCLUDES=$(pkg-config --cflags \
    gio-2.0 \
    gio-unix-2.0 \
    glib-2.0 \
    gobject-2.0 \
    gobject-introspection-1.0 \
    gtk+-3.0 \
    libffi \
    mozjs-60)
INCLUDES="-I. -I$BUILDDIR"
DEFINES="-DGJS_COMPILATION -DPKGLIBDIR=\"\" -DINSTTESTDIR=\"\" -DGJS_JS_DIR=\"\""
CPP_ARGS="$LIBS_INCLUDES $INCLUDES $DEFINES"

LANG_CXX="--language=c++ -std=c++14"
LANG_C="--language=c"
CXX_STDLIB_INCLUDES=$(clang -Wp,-v $LANG_CXX -fsyntax-only /dev/null 2>&1 | \
    grep '^ ' | sed -e 's/^ /-I/')
C_STDLIB_INCLUDES=$(clang -Wp,-v $LANG_C -fsyntax-only /dev/null 2>&1 | \
    grep '^ ' | sed -e 's/^ /-I/')
CXX_ARGS="$LANG_CXX $CXX_STDLIB_INCLUDES $CPP_ARGS"
C_ARGS="$LANG_C $C_STDLIB_INCLUDES $CPP_ARGS"

for FILE in gi/*.cpp gi/gjs_gi_trace.h gjs/atoms.cpp gjs/byteArray.cpp \
    gjs/coverage.cpp gjs/debugger.cpp gjs/deprecation.cpp gjs/engine.cpp \
    gjs/global.cpp gjs/importer.cpp gjs/jsapi-util-args.h \
    gjs/jsapi-util-error.cpp gjs/jsapi-util-root.h gjs/jsapi-util-string.cpp \
    gjs/jsapi-util.cpp gjs/module.cpp gjs/native.cpp gjs/stack.cpp \
    modules/cairo-*.cpp modules/console.cpp modules/system.cpp test/*.cpp \
    util/*.cpp
do
    iwyu $PRIVATE_MAPPING $CXX_ARGS $FILE
done
iwyu $PRIVATE_MAPPING $CXX_ARGS gjs/context.cpp \
    -Xiwyu --check_also=gjs/context-private.h
iwyu $PRIVATE_MAPPING $CXX_ARGS gjs/jsapi-dynamic-class.cpp \
    -Xiwyu --check_also=gjs/jsapi-class.h
iwyu $PRIVATE_MAPPING $CXX_ARGS gjs/mem.cpp \
    -Xiwyu --check_also=gjs/mem-private.h
iwyu $PRIVATE_MAPPING $CXX_ARGS gjs/profiler.cpp \
    -Xiwyu --check_also=gjs/profiler-private.h
iwyu $PRIVATE_MAPPING $CXX_ARGS modules/cairo.cpp \
    -Xiwyu --check_also=modules/cairo-module.h \
    -Xiwyu --check_also=modules/cairo-private.h

for FILE in gjs/console.cpp installed-tests/minijasmine.cpp; do
    iwyu $PUBLIC_MAPPING $CXX_ARGS $FILE
done

for FILE in libgjs-private/*.c; do
    iwyu $PRIVATE_MAPPING $C_ARGS $FILE
done
iwyu $PUBLIC_MAPPING $C_ARGS gjs/macros.h
