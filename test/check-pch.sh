#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>
set -e

if [ -n "$SELFTEST" ]; then
    unset SELFTEST
    set -x
    self="$(realpath "$0")"
    test_paths=()
    trap 'rm -rf -- "${test_paths[@]}"' EXIT

    test_env() {
        local code_path="$(mktemp -t -d "check-pch-XXXXXX")"
        test_paths+=("$code_path")
        cd "$code_path"
        mkdir gjs gi
        echo "#include <stlib.h>" >> gjs/gjs_pch.hh
    }

    expect_success() {
        "$self" || exit 1
    }
    expect_failure() {
        "$self" && exit 1 || true
    }

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    expect_success

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "#include <stdio.h>" >> gi/code.c
    expect_failure

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "#include <invalid1.h>" >> gi/code1.cpp
    echo "#include <invalid2.h>" >> gi/code1.c
    expect_failure

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "#include <invalid.h> // check-pch: ignore" >> gi/other-code.c
    expect_success

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "#include <invalid1.h> // NOcheck-pch: ignore" >> gi/code.c
    echo "#include <invalid2.h> // check-pch: ignoreNO" >> gi/code.c
    echo "#include <invalid3.h> // check-pch: ignore, yes" >> gi/other-code.c
    expect_failure

    test_env
    echo "#include <invalid.h>" >> gjs/gjs_pch.hh
    echo "#include <stlib.h>" >> gi/code.c
    expect_failure

    test_env
    echo "#include <invalid.h> // check-pch: ignore, yes" >> gjs/gjs_pch.hh
    echo "#include <stlib.h>" >> gi/code.c
    expect_success

    test_env
    echo "#include <invalid.h>" >> gi/ignored-file.hh
    echo "#include <stlib.h>" >> gi/code.c
    expect_success

    test_env
    echo '#  		  include  		  <stlib.h>' >> gi/code.c
    echo '#  		  include  		  "local/header.h"' >> gi/code.c
    expect_success

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo '#include "local/header.h"' >> gjs/gjs_pch.hh
    expect_failure

    test_env
    echo "#  		  include  		  <stlib.h>" >> gi/code.c
    echo "#  		  include  		  <other/include.h>" >> gi/code.c
    echo "  	   #  		  include  		  <other/include.h>" >> gi/other-file.c
    echo "# include <other/include.h>" >> gjs/gjs_pch.hh
    expect_success

    test_env
    echo "#    include    <stlib.h>" >> gi/code.c
    echo "#   	  include    		     <invalid.h>/*comment*/" >> gi/invalid-file.c
    expect_failure

    test_env
    echo "#    include    <stlib.h>" >> gi/code.c
    echo "  	   #  		  include  		  <other/include.h>" >> gi/other-file.c
    expect_failure

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "//#include <invalid.h>" >> gi/invalid-file.c
    echo "// #include <invalid.h>" >> gi/invalid-file.c
    echo "//#include <invalid.h>" >> gjs/gjs_pch.hh
    expect_success

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "/*comment*/#include <invalid.h>/*comment*/" >> gi/invalid-file.c
    # This is not supported: expect_failure

    test_env
    echo "#include <stlib.h>" >> gi/code.c
    echo "#   /*FIXME */  include  /*Why should you do it?*/  <invalid.h>" >> gi/invalid-file.c
    # This is not supported: expect_failure

    exit 0
fi

PCH_FILES=(gjs/gjs_pch.hh)
IGNORE_COMMENT="check-pch: ignore"

CODE_PATHS=(gjs gi)
INCLUDED_FILES=(
    \*.c
    \*.cpp
    \*.h
)

grep_include_lines() {
    grep -h '^\s*#\s*include\s*[<"][^>"]\+[>"]' "$@" | uniq
}

grep_header_file() {
    local header_file="${1//./\\.}"
    shift
    grep -qs "^\s*#\s*include\s*[<\"]${header_file}[>\"]" "$@"
}

# List all the included headers
mapfile -t includes < <(grep_include_lines \
    -r \
    $(printf -- "--include %s\n" "${INCLUDED_FILES[@]}") \
    "${CODE_PATHS[@]}" \
    | grep -vw "$IGNORE_COMMENT")

missing=()
for h in "${includes[@]}"; do
    if [[ "$h" =~ \#[[:space:]]*include[[:space:]]*\<([^\>]+)\> ]]; then
        header_file="${BASH_REMATCH[1]}"
        if ! grep_header_file "$header_file" "${PCH_FILES[@]}"; then
            echo "Header <$header_file> not added to PCH file"
            missing+=("$header_file")
        fi
    fi
done

if [ "${#missing[@]}" -gt 0 ]; then
    echo
    echo "Headers not added to the PCH file found, please add to ${PCH_FILES[*]}"
    echo "Otherwise you can ignore them with a leading comment such as"
    echo "  #include <${missing[0]}>  // $IGNORE_COMMENT"
    exit 1
fi

# And now, the other way around...
mapfile -t pch_includes < <(grep_include_lines \
    "${PCH_FILES[@]}" \
    | grep -vw "$IGNORE_COMMENT")

unneeded=()
for h in "${pch_includes[@]}"; do
    if [[ "$h" =~ \#[[:space:]]*include[[:space:]]*[\<\"]([^\>\"]+)[\>\"] ]]; then
        header_file="${BASH_REMATCH[1]}"
        if ! grep_header_file "$header_file" -r \
            $(printf -- "--include %s\n" "${INCLUDED_FILES[@]}") \
            "${CODE_PATHS[@]}"; then
            echo "Header <$header_file> included in one PCH is not used in code"
            unneeded+=("$header_file")
        fi
    fi
done

if [ "${#unneeded[@]}" -gt 0 ]; then
    echo
    echo "Unneeded headers added to the PCH file found, remove from ${PCH_FILES[*]}"
    echo "Otherwise you can ignore them with a leading comment such as"
    echo "  #include <${unneeded[0]}>  // $IGNORE_COMMENT"
    exit 1
fi
