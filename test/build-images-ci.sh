#!/bin/bash -e

function do_Set_Env(){
    echo
    echo '-- Set Environment --'

    #Save cache on host (outside the image), if linked
    mkdir -p /cwd/.cache
    export XDG_CACHE_HOME=/cwd/.cache

    export JHBUILD_RUN_AS_ROOT=1
    export SHELL=/bin/bash
    PATH=$PATH:~/.local/bin

    if [[ -z "${DISPLAY}" ]]; then
        export DISPLAY=":0"
    fi

    echo '-- Done --'
}

function do_Build_Package_Dependencies(){
    echo
    echo "-- Building Dependencies for $1 --"
    jhbuild list "$1"

    # Build package dependencies
    jhbuild build $(jhbuild list "$1" | sed '$d')
}

function do_Show_Info(){

    echo '--------------------------------'
    echo 'Useful build system information'
    echo -n "Processors: "; grep -c ^processor /proc/cpuinfo
    id; uname -a
    printenv
    echo '--------------------------------'
    cat /etc/*-release
    echo '--------------------------------'

    if [[ ! -z $CC ]]; then
        echo 'Compiler version'
        $CC --version
        echo '--------------------------------'
        $CC -dM -E -x c /dev/null
        echo '--------------------------------'
    fi
}

# ----------- Run the Tests -----------
if [[ -d /cwd ]]; then
    cd /cwd
else
    cd /saved
fi

if [[ -n "${BUILD_OPTS}" ]]; then
    extra_opts="($BUILD_OPTS)"
fi

# Show some environment info
echo
echo '-- Environment --'
echo "Running on: $BASE $OS  $extra_opts"
echo "Doing: $1"

source test/extra/do_basic.sh
source test/extra/do_jhbuild.sh
source test/extra/do_mozilla.sh
source test/extra/do_docker.sh

# Done
echo
echo '-- DONE --'
