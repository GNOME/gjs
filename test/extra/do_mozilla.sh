#!/bin/bash -e

function do_Build_Mozilla(){
    echo
    echo '-- Building Mozilla SpiderMonkey --'

    git clone --depth 1 https://github.com/ptomato/mozjs.git -b "${MOZJS_BRANCH:-mozjs60}" /on-host/mozjs
    cd /on-host/mozjs

    mkdir -p _build
    cd _build

    ../js/src/configure --prefix=/root/jhbuild/install --enable-posix-nspr-emulation --with-system-zlib --with-intl-api --disable-jemalloc AUTOCONF=autoconf ${BUILD_OPTS}
    make -sj4
    make install

    cd -
}
