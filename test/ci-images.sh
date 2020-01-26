#!/bin/bash -e

function do_Install_Dependencies(){
    echo
    echo '-- Installing Base Dependencies --'

    dnf -y upgrade --best --allowerasing

    # Base dependencies: CI scripts, mozjs, gjs
    # mozjs and gjs build dependencies adapted from the lists in:
    # https://src.fedoraproject.org/rpms/mozjs68/blob/master/f/mozjs68.spec
    # https://src.fedoraproject.org/rpms/gjs/blob/master/f/gjs.spec
    dnf -y install @c-development @development-tools clang compiler-rt \
        gnome-desktop-testing lcov libasan libubsan libtsan meson ninja-build \
        systemtap-sdt-devel Xvfb xz \
        \
        cargo clang-devel llvm llvm-devel perl-devel 'pkgconfig(libffi)' \
        'pkgconfig(zlib)' python2-devel readline-devel rust which zip \
        \
        autoconf-archive cairo-gobject-devel diffutils dbus-daemon dbus-x11 \
        dbus-glib-devel glib2-devel gobject-introspection-devel gtk3-devel \
        sysprof-devel

    # Debuginfo needed for stack traces, e.g. in Valgrind
    dnf -y debuginfo-install glib2-devel gobject-introspection-devel \
        gtk3-devel fontconfig cairo glibc
}

function do_Set_Env(){
    echo
    echo '-- Set Environment --'

    #Save cache on host (outside the image), if linked
    mkdir -p /on-host/.cache
    export XDG_CACHE_HOME=/on-host/.cache

    export SHELL=/bin/bash
    PATH=$PATH:~/.local/bin

    if [[ -z "${DISPLAY}" ]]; then
        export DISPLAY=":0"
    fi

    dbus-uuidgen > /var/lib/dbus/machine-id

    echo '-- Done --'
}

function do_Build_Mozilla(){
    echo
    echo '-- Building Mozilla SpiderMonkey --'

    git clone --depth 1 https://github.com/ptomato/mozjs.git -b "${MOZJS_BRANCH:-mozjs68}" /on-host/mozjs
    cd /on-host/mozjs

    mkdir -p _build
    cd _build

    ../js/src/configure --prefix=/usr/local \
         --enable-posix-nspr-emulation \
         --with-system-zlib \
         --with-intl-api \
         --disable-jemalloc \
         AUTOCONF=autoconf \
         ${BUILD_OPTS}
    make -sj4
    make install

    cd -
}

function do_Shrink_Image(){
    echo
    echo '-- Cleaning image --'
    PATH=$PATH:~/.local/bin
    rm -rf ~/jhbuild/install/lib/libjs_static.ajs

    dnf -y clean all
    rm -rf /var/cache/dnf

    echo '-- Done --'
}

# ----------- Run the Tests -----------
cd /on-host

if [[ -n "${BUILD_OPTS}" ]]; then
    extra_opts="($BUILD_OPTS)"
fi

if [[ -n "${MOZJS_BRANCH}" ]]; then
    extra_opts="$extra_opts  ($MOZJS_BRANCH)"
fi

source test/extra/do_environment.sh

# Show some environment info
do_Print_Labels  'ENVIRONMENT'
echo "Running on: $OS"
echo "Doing: $1 $extra_opts"

do_Install_Dependencies
do_Set_Env
do_Show_Info
do_Build_Mozilla
do_Shrink_Image

# Clear the environment
unset BUILD_OPTS

# Releases stuff and finishes
do_Done
