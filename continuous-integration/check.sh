#!/bin/bash -e

# Get and install dependencies
echo
echo '-- Prepare --'
echo "Running on: $DISTRO $OS"
cd /cwd

if [[ $DISTRO == "ubuntu" ]]; then
    apt-get update -qq
    apt-get -y install build-essential
    apt-get -y install libmount-dev libpcre3-dev python-dev dh-autoreconf flex bison pkg-config zlib1g-dev libffi-dev
    apt-get -y install libxml2-utils libgamin-dev
    apt-get -y install software-properties-common
    add-apt-repository -y ppa:ricotz/testing
    apt-get update
    apt-get -y install libmozjs-31-dev
else
    exit 1
fi

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
./autogen.sh --prefix=/usr/local
make -sj2
make install

# Test the build
echo
echo '-- Testing GJS --'
ls -la /usr/local/libexec/gjs
