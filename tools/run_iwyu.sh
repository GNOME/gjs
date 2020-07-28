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

if ! meson setup _build; then
    echo 'Meson failed.'
    exit 1
fi
cd ${BUILDDIR:-_build}

echo "files: $files"

IWYU="python3 $(which iwyu_tool) -p ."
IWYU_RAW="include-what-you-use -xc++ -std=c++17"
PRIVATE_MAPPING="-Xiwyu --mapping_file=$SRCDIR/tools/gjs-private-iwyu.imp -Xiwyu --keep=config.h"
PUBLIC_MAPPING="-Xiwyu --mapping_file=$SRCDIR/tools/gjs-public-iwyu.imp"
POSTPROCESS="python3 $SRCDIR/tools/process_iwyu.py"
EXIT=0

# inline-only files with no implementation file don't appear in the compilation
# database, so iwyu_tool cannot process them
if should_analyze $SRCDIR/gi/utils-inl.h; then
    if ! $IWYU_RAW $(realpath --relative-to=. $SRCDIR/gi/utils-inl.h) 2>&1 \
        | $POSTPROCESS; then
        EXIT=1
    fi
fi

for FILE in $SRCDIR/gi/*.cpp $SRCDIR/gjs/atoms.cpp $SRCDIR/gjs/byteArray.cpp \
    $SRCDIR/gjs/coverage.cpp $SRCDIR/gjs/debugger.cpp \
    $SRCDIR/gjs/deprecation.cpp $SRCDIR/gjs/error-types.cpp \
    $SRCDIR/gjs/engine.cpp $SRCDIR/gjs/global.cpp $SRCDIR/gjs/importer.cpp \
    $SRCDIR/gjs/jsapi-util-error.cpp $SRCDIR/gjs/jsapi-util-string.cpp \
    $SRCDIR/gjs/module.cpp $SRCDIR/gjs/native.cpp $SRCDIR/gjs/stack.cpp \
    $SRCDIR/modules/cairo-*.cpp $SRCDIR/modules/console.cpp \
    $SRCDIR/modules/print.cpp $SRCDIR/modules/system.cpp $SRCDIR/test/*.cpp \
    $SRCDIR/util/*.cpp $SRCDIR/libgjs-private/*.c
do
    if should_analyze $FILE; then
        if ! $IWYU $FILE -- $PRIVATE_MAPPING | $POSTPROCESS; then
            EXIT=1
        fi
    fi
done

if should_analyze $SRCDIR/gjs/context.cpp; then
    if ! $IWYU $SRCDIR/gjs/context.cpp -- $PRIVATE_MAPPING \
        -Xiwyu --check_also=*/gjs/context-private.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

if ( should_analyze $SRCDIR/gjs/jsapi-dynamic-class.cpp || \
    should_analyze $SRCDIR/gjs/jsapi-class.h ); then
    if ! $IWYU $SRCDIR/gjs/jsapi-dynamic-class.cpp -- $PRIVATE_MAPPING \
        -Xiwyu --check_also=*/gjs/jsapi-class.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

if ( should_analyze $SRCDIR/gjs/jsapi-util.cpp ||
    should_analyze $SRCDIR/gjs/jsapi-util-args.h || \
    should_analyze $SRCDIR/gjs/jsapi-util-root.h ); then
    if ! $IWYU $SRCDIR/gjs/jsapi-util.cpp -- $PRIVATE_MAPPING \
        -Xiwyu --check_also=*/gjs/jsapi-util-args.h \
        -Xiwyu --check_also=*/gjs/jsapi-util-root.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

if should_analyze $SRCDIR/gjs/mem.cpp; then
    if ! $IWYU $SRCDIR/gjs/mem.cpp -- $PRIVATE_MAPPING \
        -Xiwyu --check_also=*/gjs/mem-private.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

if should_analyze $SRCDIR/gjs/profiler.cpp; then
    if ! $IWYU $SRCDIR/gjs/profiler.cpp -- $PRIVATE_MAPPING \
        -Xiwyu --check_also=*/gjs/profiler-private.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

if ( should_analyze $SRCDIR/modules/cairo.cpp ||
    should_analyze $SRCDIR/modules/cairo-module.h ); then
    if ! $IWYU $SRCDIR/modules/cairo.cpp -- $PRIVATE_MAPPING \
        -Xiwyu --check_also=*/modules/cairo-module.h \
        -Xiwyu --check_also=*/modules/cairo-private.h | $POSTPROCESS; then
        EXIT=1
    fi
fi

for FILE in $SRCDIR/gjs/console.cpp $SRCDIR/installed-tests/minijasmine.cpp
do
    if should_analyze $FILE; then
        if ! $IWYU $FILE -- $PUBLIC_MAPPING | $POSTPROCESS; then
            EXIT=1
        fi
    fi
done

if test $EXIT -eq 0; then echo "No changes needed."; fi
exit $EXIT
