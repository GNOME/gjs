#!/bin/bash
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

cd "$(dirname -- "$0")/.." || exit 1

[ $# -eq 0 ] && set -- .

filters=(
    build/include_what_you_use
    readability/braces:modules/cairo-region.cpp
      # https://github.com/cpplint/cpplint/issues/406
    runtime/int  # In many places required by GLib APIs
    whitespace/indent_namespace  # https://github.com/cpplint/cpplint/issues/372
)
filter_arg=$(printf ",-%s" "${filters[@]}")
filter_arg="--filter=${filter_arg:1}"  # remove leading comma

exclude=(
    subprojects/
    "_*build/"
    gi/gjs_gi_trace.h
    gjs/gjs_pch.hh
)
mapfile -t exclude_args < <(printf -- "--exclude=%s\n" "${exclude[@]}")

cpplint --quiet --recursive "${exclude_args[@]}" "$filter_arg" "$@"
