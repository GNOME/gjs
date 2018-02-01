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
source test/extra/do_jhbuild.sh
source test/extra/do_cache.sh
source test/extra/do_mozilla.sh
source test/extra/do_docker.sh

# Show some environment info
echo
echo '-- Environment --'
echo "Running on: $BASE $OS"
echo "Doing: $1"

if [[ $1 == "GJS" ]]; then
    do_Set_Env

    do_Show_Info
    do_Patch_JHBuild
    do_Build_JHBuild
    do_Configure_JHBuild

    if [[ $2 != "devel" ]]; then
        do_Build_Package_Dependencies gjs
    else
      jhbuild build m4-common
      mkdir -p ~/jhbuild/checkout/gjs
      do_Install_Extras
    fi

    # Build and test the latest commit (merged or from a merge/pull request) of
    # Javascript Bindings for GNOME (gjs)
    echo
    echo '-- gjs status --'
    cp -r ./ ~/jhbuild/checkout/gjs

    cd ~/jhbuild/checkout/gjs
    git log --pretty=format:"%h %cd %s" -1

    echo
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

elif [[ $1 == "GJS_COVERAGE" ]]; then
    # Code coverage test. It doesn't (re)build, just run the 'Coverage Tests'
    echo
    echo '-- Code Coverage Report --'
    do_Set_Env
    PATH=$PATH:~/.local/bin

    jhbuild run --in-builddir=gjs make check-code-coverage
    mkdir -p /cwd/coverage
    cp /cwd/.cache/jhbuild/build/gjs/gjs-?.*.*-coverage.info /cwd/coverage/
    cp -r /cwd/.cache/jhbuild/build/gjs/gjs-?.*.*-coverage/* /cwd/coverage/

    echo '-----------------------------------------------------------------'
    sed -e 's/<[^>]*>//g' /cwd/coverage/index.html | tr -d ' \t' | grep -A3 -P '^Lines:$'  | tr '\n' ' '; echo
    echo '-----------------------------------------------------------------'

elif [[ $1 == "CPPCHECK" ]]; then
    echo
    echo '-- Static code analyzer report --'
    cppcheck --inline-suppr --enable=warning,performance,portability,information,missingInclude --force -q . 2>&1 | \
        sed -E 's/:[0-9]+]/:LINE]/' | tee /cwd/current-report.txt
    echo

    echo '-- Master static code analyzer report --'
    git clone --depth 1 https://gitlab.gnome.org/GNOME/gjs.git tmp-upstream; cd tmp-upstream || exit 1
    cppcheck --inline-suppr --enable=warning,performance,portability,information,missingInclude --force -q . 2>&1 | \
        sed -E 's/:[0-9]+]/:LINE]/' | tee /cwd/master-report.txt
    echo

    # Compare the report with master and fails if new warnings is found
    if ! diff --brief /cwd/master-report.txt /cwd/current-report.txt > /dev/null; then
        echo '----------------------------------------'
        echo '###  New warnings found by cppcheck  ###'
        echo '----------------------------------------'
        diff -u /cwd/master-report.txt /cwd/current-report.txt || true
        echo '----------------------------------------'
        exit 3
    fi
fi
# Done
echo
echo '-- DONE --'
