#!/bin/bash -e

function do_Set_Env(){
    echo
    echo '-- Set Environment --'

    #Save cache on host
    mkdir -p /cwd/.cache
    export XDG_CACHE_HOME=/cwd/.cache
    export JHBUILD_RUN_AS_ROOT=1
    export SHELL=/bin/bash

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
cd /cwd

source test/extra/do_basic.sh
source test/extra/do_docker.sh
source test/extra/do_jhbuild.sh
source test/extra/do_cache.sh
source test/extra/do_mozilla.sh

# Show some environment info
echo
echo '-- Environment --'
echo "Running on: $BASE $OS"
echo "Doing: $1"

if [[ $1 == "BUILD_MOZ" ]]; then
    do_Install_Base_Dependencies
    do_Set_Env

    do_Show_Info
    do_Patch_JHBuild
    do_Build_JHBuild RESET
    do_Build_Mozilla
    do_Save_Files

    if [[ $2 == "SHRINK" ]]; then
        do_Shrink_Image
    fi

elif [[ $1 == "GET_FILES" ]]; then
    do_Set_Env
    do_Get_Files

    if [[ $2 == "DOCKER" ]]; then
        do_Install_Base_Dependencies
        do_Install_Dependencies
        do_Shrink_Image
    fi

elif [[ $1 == "INSTALL_GIT" ]]; then
    do_Install_Git

elif [[ $1 == "GJS" ]]; then
    do_Set_Env

    do_Show_Info
    do_Patch_JHBuild
    do_Build_JHBuild
    do_Configure_JHBuild
    do_Build_Package_Dependencies gjs

    # Build and test the latest commit (merged or from a merge/pull request) of
    # Javascript Bindings for GNOME (gjs)
    echo
    echo '-- gjs status --'
    cp -r ./ ~/jhbuild/checkout/gjs

    cd ~/jhbuild/checkout/gjs
    git log --pretty=format:"%h %cd %s" -1

    echo '-- gjs build --'
    echo
    jhbuild make --check

elif [[ $1 == "GJS_EXTRA" ]]; then
    # Extra testing. It doesn't (re)build, just run the 'Installed Tests'
    echo
    echo '-- Installed GJS tests --'
    do_Set_Env
    PATH=$PATH:~/.local/bin

    xvfb-run jhbuild run dbus-run-session -- gnome-desktop-testing-runner gjs

elif [[ $1 == "CPPCHECK" ]]; then
    echo
    echo '-- Code analyzer --'
    cppcheck --enable=warning,performance,portability,information,missingInclude --force -q .
    echo

else
    echo
    echo '-- NOTHING TO DO --'
    exit 1
fi
# Done
echo
echo '-- DONE --'
