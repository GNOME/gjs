#!/bin/bash -e

function do_Get_JHBuild(){
    echo
    echo '-- Download JHBuild --'

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
    echo
    echo '-- Set JHBuild Configuration --'

    mkdir -p ~/.config

    cat <<EOFILE > ~/.config/jhbuildrc
skip = ['gettext', 'yelp-xsl', 'yelp-tools', 'gtk-doc']
use_local_modulesets = True
EOFILE

    echo '-- Done --'
}

function do_Configure_MainBuild(){
    echo
    echo '-- Set Main JHBuild Configuration --'

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

function do_Build_JHBuild(){
    echo
    echo '-- Building JHBuild --'

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
