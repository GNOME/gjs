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
