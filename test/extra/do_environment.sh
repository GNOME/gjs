#!/bin/bash -e

function do_Configure_MainBuild(){
    do_Print_Labels 'Set Main Build Configuration'

    autogenargs="--enable-compile-warnings=yes"

    if [[ -n "${BUILD_OPTS}" ]]; then
        autogenargs="$autogenargs $BUILD_OPTS"
    fi
    export ci_autogenargs="$autogenargs"

    echo '-- Done --'
}

function do_Print_Labels(){

    if [[ -n "${1}" ]]; then
        label_len=${#1}
        span=$(((54 - $label_len) / 2))

        echo
        echo "= ======================================================== ="
        printf "%s %${span}s %s %${span}s %s\n" "=" "" "$1" "" "="
        echo "= ======================================================== ="
    else
        echo "= ========================= Done ========================= ="
        echo
    fi
}

function do_Done(){

    # Done. De-initializes whatever is needed
    do_Print_Labels  'FINISHED'
}

function do_Show_Info(){

    local compiler=gcc

    echo '-----------------------------------------'
    echo 'Build system information'
    echo -n "Processors: "; grep -c ^processor /proc/cpuinfo
    grep ^MemTotal /proc/meminfo
    id; uname -a
    printenv
    echo '-----------------------------------------'
    cat /etc/*-release
    echo '-----------------------------------------'

    if [[ ! -z $CC ]]; then
        compiler=$CC
    fi
    echo 'Compiler version'
    $compiler --version
    echo '-----------------------------------------'
    $compiler -dM -E -x c /dev/null
    echo '-----------------------------------------'
}
