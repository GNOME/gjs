#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2024 Philip Chimento <philip.chimento@gmail.com>

# Drafted with assistance from ChatGPT.
# https://chat.openai.com/share/0cd77782-13b5-4775-80d0-c77c7749fb9d

if [ -n "$SELFTEST" ]; then
    unset SELFTEST
    set -x
    self="$(realpath "$0")"
    test_paths=()
    trap 'rm -rf -- "${test_paths[@]}"' EXIT

    test_env() {
        local code_path
        code_path=$(mktemp -d -t "check-pch-XXXXXX")
        test_paths+=("$code_path")
        cd "$code_path" || exit
        mkdir gi gjs libgjs-private modules test util
    }

    expect_success() {
        "$self" || exit 1
    }
    expect_failure() {
        "$self" && exit 1 || true
    }

    # config.h is included
    test_env
    echo "#include <config.h>" > gjs/program.c
    expect_success

    # config.h must be in angle brackets
    test_env
    echo '#include "config.h"' > gjs/program.c
    expect_failure

    # public headers are skipped
    test_env
    echo "#include <stdlib.h>" > gjs/macros.h
    expect_success

    # config.h must be included
    test_env
    echo "#include <stdlib.h>" > gjs/program.c
    expect_failure

    # config.h is included first
    test_env
    echo '#include <config.h>' > gjs/program.c
    echo '#include <stdlib.h>' >> gjs/program.c
    expect_success

    # config.h must be included first
    test_env
    echo '#include <stdlib.h>' > gjs/program.c
    echo '#include <config.h>' >> gjs/program.c
    expect_failure

    # other non-include things can come before the include
    test_env
    cat > gjs/program.h <<EOF
/* a comment */
#pragma once
#include <config.h>
EOF
    expect_success

    # spaces are taken into account
    test_env
    cat > gjs/program.c <<EOF
#ifdef UNIX
#  include <unix.h>
#endif
#include <config.h>
EOF
    expect_failure

    # header blocks in right order
    test_env
    cat > gjs/program.c <<EOF
#include <config.h>
#include <stdint.h>
#include <memory>
#include <glib.h>
#include <js/TypeDecls.h>
#include "program.h"
EOF
    expect_success

    # header blocks in wrong order
    test_env
    cat > gjs/program.c <<EOF
#include <config.h>
#include <memory>
#include <glib.h>
#include <js/TypeDecls.h>
#include "program.h"
#include <stdint.h>
EOF
    expect_failure

    exit 0
fi

failed=0

function report_out_of_order {
    file="$1"
    shift
    include="$1"
    shift
    descr="$1"
    shift
    headers=("$@")

    if [[ ${#headers[@]} -ne 0 ]]; then
        echo "Error: $file: include $include before $descr header ${headers[0]}"
        failed=1
        return 1
    fi
    return 0
}

function check_config_header {
    file="$1"
    included_files=($(sed -nE 's/^#[[:space:]]*include[[:space:]]*([<"][^>"]+[>"]).*/\1/p' "$file"))
    if [[ "${included_files[0]}" != "<config.h>" ]]; then
        echo "Error: $file: include <config.h> as the first #include directive"
        failed=1
    fi

    c_headers=()
    cpp_headers=()
    gnome_headers=()
    moz_headers=()
    gjs_headers=()
    for include in "${included_files[@]:1}"; do
        if [[ "$include" =~ \".*\.h\" ]]; then
            gjs_headers+=("$include")
            continue
        fi
        report_out_of_order "$file" "$include" "GJS" "${gjs_headers[@]}" || continue
        if [[ "$include" =~ \<(js|mozilla).*\.h\> ]]; then
            moz_headers+=("$include")
            continue
        fi
        report_out_of_order "$file" "$include" "Mozilla" "${moz_headers[@]}" || continue
        if [[ "$include" =~ \<(ffi|sysprof.*|cairo.*|g.*)\.h\> ]]; then
            gnome_headers+=("$include")
            continue
        fi
        report_out_of_order "$file" "$include" "GNOME platform" "${gnome_headers[@]}" || continue
        if [[ "$include" =~ \<.*\.h\> ]]; then
            report_out_of_order "$file" "$include" "C++ standard library" "${cpp_headers[@]}" || continue
            c_headers+=("$include")
        elif [[ "$include" =~ \<.*\> ]]; then
            cpp_headers+=("$include")
        else
            echo "Error: Need to fix your regex to handle $include."
            failed=1
        fi
    done
}

files=$(find gi gjs libgjs-private modules test util \
    -name '*.c' -o -name '*.cpp' -o -name '*.h')
for file in $files; do
    if [[ "$file" == "gjs/gjs.h" || "$file" == "gjs/macros.h" ]]; then continue; fi
    if grep -ql "^GJS_EXPORT" "$file"; then continue; fi
    check_config_header "$file"
done

if [[ $failed -ne 0 ]]; then
    echo "Errors found."
    exit 1
else
    echo "OK."
fi
