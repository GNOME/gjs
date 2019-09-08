#!/bin/bash -e

function do_Install_Dependencies(){
    echo
    echo '-- Installing Base Dependencies --'

    dnf -y upgrade --best --allowerasing

    # Base dependencies
    dnf -y    install @c-development @development-tools clang redhat-rpm-config gnome-common python-devel \
                      pygobject2 dbus-python perl-Text-CSV perl-XML-Parser gettext-devel gtk-doc \
                      autoconf-archive meson ninja-build zlib-devel libffi-devel \
                      libtool libicu-devel nspr-devel systemtap-sdt-devel \
                      gtk3 gtk3-devel gobject-introspection-devel Xvfb gnome-desktop-testing dbus-x11 \
                      cairo intltool libxslt bison nspr python3-devel dbus-glib libicu \
                      libxslt libtool flex \
                      cairo-devel zlib-devel libffi-devel pcre-devel libxml2-devel libxslt-devel \
                      libedit-devel libasan libubsan libtsan compiler-rt \
                      sysprof-devel lcov mesa-libGL-devel readline-devel \
                      webkit2gtk3 time

    # Distros debug info of needed libraries
    dnf -y debuginfo-install glib2-devel gobject-introspection-devel \
      gtk3-devel expat fontconfig cairo glibc
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

    echo '-- Done --'
}

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

if [[ $1 == "BUILD_MOZ" ]]; then
    do_Install_Dependencies
    do_Set_Env
    do_Show_Info
    do_Build_Mozilla
    do_Shrink_Image
fi
# Clear the environment
unset BUILD_OPTS

# Releases stuff and finishes
do_Done
