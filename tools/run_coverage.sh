#!/bin/bash

SOURCEDIR=$(pwd)
GIDATADIR=/Users/ptomato/jhbuild/install/share/gobject-introspection-1.0

BUILDDIR="$(pwd)/_coverage_build"
LCOV_ARGS="--config-file $SOURCEDIR/tools/lcovrc"
GENHTML_ARGS='--legend --show-details --branch-coverage'
IGNORE="*/gjs/test/* *-resources.c *minijasmine.cpp"

rm -rf "$BUILDDIR"
meson "$BUILDDIR" -Db_coverage=true
ninja -C "$BUILDDIR"
mkdir -p _coverage
ninja -C "$BUILDDIR" test
lcov --directory "$BUILDDIR" --capture --output-file _coverage/gjs.lcov.run --no-checksum $LCOV_ARGS
lcov --extract _coverage/gjs.lcov.run "$SOURCEDIR/*" "$GIDATADIR/tests/*" $LCOV_ARGS -o _coverage/gjs.lcov.sources
lcov --remove _coverage/gjs.lcov.sources $IGNORE $LCOV_ARGS -o _coverage/gjs.lcov
genhtml --prefix "$BUILDDIR/lcov/org/gnome/gjs" --prefix "$BUILDDIR" --prefix "$SOURCEDIR" --prefix "$GIDATADIR" --output-directory _coverage/html --title "gjs-x.y.z Code Coverage" $GENHTML_ARGS _coverage/gjs.lcov "$BUILDDIR"/lcov/coverage.lcov
