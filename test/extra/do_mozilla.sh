#!/bin/bash -e

function do_Configure_MozBuild(){
    echo
    echo '-- Set JHBuild Configuration --'

    mkdir -p ~/.config
    autogenargs=""

    if [[ -n "${BUILD_OPTS}" ]]; then
        autogenargs="$autogenargs $BUILD_OPTS"
    fi

    cat <<EOFILE >> ~/.config/jhbuildrc
module_autogenargs['mozjs60'] = "$autogenargs"
module_makeargs['mozjs60'] = '-s'
EOFILE

    echo '-- Done --'
}

function do_Build_Mozilla_jhbuild(){
    echo
    echo '-- Building Mozilla SpiderMonkey --'

    # Configure the Mozilla build
    do_Configure_MozBuild

    # Build Mozilla Stuff
    jhbuild build mozjs60
}

function do_Build_Mozilla(){
    echo
    echo '-- Building Mozilla SpiderMonkey --'

    git clone --depth 1 https://github.com/ptomato/mozjs.git -b "${MOZJS_BRANCH:-mozjs60}" /on-host/spider
    cd /on-host/spider

    mkdir -p _build
    cd _build

    ../js/src/configure --prefix=/root/jhbuild/install --enable-posix-nspr-emulation --with-system-zlib --with-intl-api --disable-jemalloc AUTOCONF=autoconf ${BUILD_OPTS}
    make -sj4
    make install

    cd -
}
