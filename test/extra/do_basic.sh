#!/bin/bash -e

function do_Install_Dependencies(){
    echo
    echo '-- Installing Base Dependencies --'

    if [[ $BASE == "debian" ]]; then
        apt-get update

        # Base dependencies
        apt-get -y     install build-essential git clang patch bison flex \
                               ninja-build python-dev python3-dev \
                               autotools-dev autoconf gettext pkgconf autopoint yelp-tools \
                               docbook docbook-xsl libtext-csv-perl \
                               zlib1g-dev libdbus-glib-1-dev \
                               libtool libicu-dev libnspr4-dev \
                               policykit-1 python3-setuptools \
                               libgtk-3-dev gir1.2-gtk-3.0 xvfb gnome-desktop-testing dbus-x11 dbus \
                               libedit-dev libgl1-mesa-dev lcov libreadline-dev

    elif [[ $BASE == "fedora" ]]; then
        if [[ $STATIC == *"qemu"* ]]; then
            dnf -y --nogpgcheck upgrade --best --allowerasing
        else
            dnf -y upgrade --best --allowerasing
        fi

        # Base dependencies
        dnf -y    install @c-development @development-tools clang redhat-rpm-config gnome-common python-devel \
                          pygobject2 dbus-python perl-Text-CSV perl-XML-Parser gettext-devel gtk-doc \
                          ninja-build zlib-devel libffi-devel \
                          libtool libicu-devel nspr-devel systemtap-sdt-devel \
                          gtk3 gtk3-devel gobject-introspection Xvfb gnome-desktop-testing dbus-x11 \
                          cairo intltool libxslt bison nspr python3-devel dbus-glib libicu \
                          libxslt libtool flex \
                          cairo-devel zlib-devel libffi-devel pcre-devel libxml2-devel libxslt-devel \
                          libedit-devel libasan libubsan libtsan compiler-rt \
                          sysprof-devel lcov mesa-libGL-devel readline-devel \
                          webkit2gtk3

        if [[ $DEV == "devel" ]]; then
            dnf -y install time
        fi
    else
        echo
        echo '-- Error: invalid BASE code --'
        exit 1
    fi
}

function do_Install_Extras(){
    echo
    echo '-- Installing Extra Dependencies --'

    if [[ $BASE == "debian" ]]; then
        # Distros development versions of needed libraries
        apt-get -y install libgirepository1.0-dev libwebkit2gtk-4.0-dev notify-osd

    elif [[ $BASE == "fedora" ]]; then
        # Distros development versions of needed libraries
        dnf -y install gobject-introspection-devel

        if [[ $STATIC != *"qemu"* ]]; then
            # Distros debug info of needed libraries
            dnf -y debuginfo-install glib2-devel gobject-introspection-devel gtk3-devel expat fontconfig cairo glibc
        fi
    fi
}
