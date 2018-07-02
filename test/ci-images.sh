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

# ----------- Run the Tests -----------
cd /saved

if [[ -n "${BUILD_OPTS}" ]]; then
    extra_opts="($BUILD_OPTS)"
fi

if [[ -n "${STATIC}" ]]; then
    extra_opts="$extra_opts  ($STATIC)"
fi

source test/extra/do_environment.sh

# Show some environment info
do_Print_Labels  'ENVIRONMENT'
echo "Running on: $BASE $OS"
echo "Doing: $1 $extra_opts"

source test/extra/do_basic.sh
source test/extra/do_mozilla.sh
source test/extra/do_docker.sh

# Releases stuff and finishes
do_Done
