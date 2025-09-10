#!/bin/bash
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

SOURCEDIR=$(pwd)
BUILDDIR="$(pwd)/_coverage_build"
LCOV_ARGS="--config-file $SOURCEDIR/tools/lcovrc"
GENHTML_ARGS='--legend --show-details --branch-coverage'
IGNORE="*/gjs/test/* *-resources.c *minijasmine.cpp */gjs/installed-tests/js/libgjstesttools/* */gjs/subprojects/glib/* */gjs/_coverage_build/subprojects/glib/*  */gjs/subprojects/gobject-introspection/*"

rm -rf "$BUILDDIR"
meson setup "$BUILDDIR" -Db_coverage=true

VERSION=$(meson introspect "$BUILDDIR" --projectinfo | python -c 'import json, sys; print(json.load(sys.stdin)["version"])')

mkdir -p _coverage
meson test -C "$BUILDDIR" --num-processes 1 --suite=gjs
lcov --directory "$BUILDDIR" --capture --output-file _coverage/gjs.lcov.run --no-checksum $LCOV_ARGS
lcov --extract _coverage/gjs.lcov.run "$SOURCEDIR/*" $LCOV_ARGS -o _coverage/gjs.lcov.sources
lcov --remove _coverage/gjs.lcov.sources $IGNORE $LCOV_ARGS -o _coverage/gjs.lcov
genhtml --prefix "$BUILDDIR/lcov/org/gnome/gjs" --prefix "$BUILDDIR" --prefix "$SOURCEDIR" \
    --output-directory _coverage/html \
    --title "gjs-$VERSION Code Coverage" \
    $GENHTML_ARGS _coverage/gjs.lcov "$BUILDDIR"/lcov/coverage.lcov
