#!/bin/bash -e

function do_Get_JHBuild(){
    do_Print_Labels 'Download JHBuild'

    if [[ -d /jhbuild ]]; then
        # For a clean build, update and rebuild jhbuild. And avoid git pull.
        rm -rf /jhbuild
    fi
    git clone --depth 1 https://github.com/GNOME/jhbuild.git /jhbuild

    # A patch is no longer required
    cd /jhbuild

    echo '-- Done --'
    cd -
}

function do_Configure_JHBuild(){
    do_Print_Labels 'Set JHBuild Configuration'

    mkdir -p ~/.config

    cat <<EOFILE > ~/.config/jhbuildrc
skip = ['gettext', 'yelp-xsl', 'yelp-tools', 'gtk-doc']
use_local_modulesets = True
EOFILE

    echo '-- Done --'
}

function do_Configure_MainBuild(){
    do_Print_Labels 'Set Main JHBuild Configuration'

    mkdir -p ~/.config
    autogenargs="--enable-compile-warnings=yes --with-xvfb-tests"

    if [[ -n "${BUILD_OPTS}" ]]; then
        autogenargs="$autogenargs $BUILD_OPTS"
    fi
    export ci_autogenargs="$autogenargs"

    cat <<EOFILE > ~/.config/jhbuildrc
module_autogenargs['gjs'] = "$autogenargs"
module_makeargs['gjs'] = '-s -j 1'
skip = ['gettext', 'yelp-xsl', 'yelp-tools', 'gtk-doc']
use_local_modulesets = True
disable_Werror = False
EOFILE

    echo '-- Done --'
}

function do_Build_Package_Dependencies(){
    do_Print_Labels "Building Dependencies for $1"
    jhbuild list "$1"

    # Build package dependencies
    jhbuild build $(jhbuild list "$1" | sed '$d')
}

function do_Build_JHBuild(){
    do_Print_Labels 'Building JHBuild'

    # Build JHBuild
    cd /jhbuild
    git log --pretty=format:"%h %cd %s" -1
    echo
    ./autogen.sh
    make -sj2
    make install
    PATH=$PATH:~/.local/bin

    if [[ $1 == "RESET" ]]; then
        git reset --hard HEAD
    fi
    echo '-- Done --'
    cd -
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
