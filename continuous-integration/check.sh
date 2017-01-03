#!/bin/bash -e

# Get and install dependencies
cd /cwd

if [[ $BASE == "ubuntu" ]]; then
    apt-get update -qq
    apt-get -y install build-essential
    apt-get -y install libmount-dev libpcre3-dev python-dev dh-autoreconf flex bison pkg-config zlib1g-dev libffi-dev
    apt-get -y install libxml2-utils libgamin-dev xvfb clang
    apt-get -y install software-properties-common
    add-apt-repository -y ppa:ricotz/testing
    apt-get update
    apt-get -y install libmozjs-31-dev
else
    exit 1
fi

# Show some environment info
echo
echo '-- Environment --'
echo "Running on: $BASE $OS"
$CC --version

# Build glib 2.0
echo
echo '-- gLib build --'
cd glib
./autogen.sh
make -sj2
make install

# Build gObject Introspection
echo
echo '-- gObject Instrospection build --'
cd ../gobject-introspection
./autogen.sh
make -sj2
make install

# Build Javascript Bindings for GNOME
echo
echo '-- gjs build --'
cd ../gjs
./autogen.sh --enable-Werror --enable-installed-tests --with-xvfb-tests # --fPIC needed for clang 3.8.1
make -sj2
make install

# Test the build
echo
echo '-- Testing GJS --'
ls -la /usr/local/libexec/gjs/installed-tests
#make check  ==> make check fails, # ERROR: 2
#gnome-desktop-testing-runner gjs ==> Is this what I have to run?

