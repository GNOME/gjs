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
