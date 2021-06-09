# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

# === Build Spidermonkey stage ===

FROM registry.fedoraproject.org/fedora:33 AS mozjs-build
ARG MOZJS_BRANCH=mozjs78
ARG MOZJS_BUILDDEPS=${MOZJS_BRANCH}
ARG BUILD_OPTS=

ENV SHELL=/bin/bash

RUN dnf -y install 'dnf-command(builddep)' autoconf213 git make which llvm-devel
RUN dnf -y builddep ${MOZJS_BUILDDEPS}

WORKDIR /root

RUN git clone --no-tags --depth 1 https://github.com/ptomato/mozjs.git -b ${MOZJS_BRANCH}
RUN mkdir -p mozjs/_build

WORKDIR /root/mozjs/_build

RUN ../js/src/configure --prefix=/usr --libdir=/usr/lib64 --disable-jemalloc \
    --with-system-zlib --with-intl-api AUTOCONF=autoconf ${BUILD_OPTS}
RUN make -j$(nproc)
RUN DESTDIR=/root/mozjs-install make install
RUN rm -f /root/mozjs-install/usr/lib64/libjs_static.ajs

# === Actual Docker image ===

FROM registry.fedoraproject.org/fedora:33

ENV SHELL=/bin/bash

# List is comprised of base dependencies for CI scripts, gjs, and debug packages
# needed for informative stack traces, e.g. in Valgrind.
#
# Do everything in one RUN command so that the dnf cache is not cached in the
# final Docker image.
RUN dnf -y install --enablerepo=fedora-debuginfo,updates-debuginfo \
    binutils cairo-debuginfo cairo-debugsource cairo-gobject-devel clang \
    compiler-rt dbus-daemon dbus-x11 diffutils fontconfig-debuginfo \
    fontconfig-debugsource gcc-c++ git glib2-debuginfo glib2-debugsource \
    glib2-devel glibc-debuginfo glibc-debuginfo-common gnome-desktop-testing \
    gobject-introspection-debuginfo gobject-introspection-debugsource \
    gobject-introspection-devel gtk3-debuginfo gtk3-debugsource gtk3-devel \
    gtk4-debuginfo gtk4-debugsource gtk4-devel lcov libasan libubsan libtsan \
    meson ninja-build pkgconf readline-devel systemtap-sdt-devel valgrind \
    which Xvfb xz && \
    dnf clean all && rm -rf /var/cache/dnf

COPY --from=mozjs-build /root/mozjs-install/usr /usr

# Enable sudo for wheel users
RUN sed -i -e 's/# %wheel/%wheel/' -e '0,/%wheel/{s/%wheel/# %wheel/}' \
    /etc/sudoers

ENV HOST_USER_ID 5555
RUN useradd -u $HOST_USER_ID -G wheel -ms /bin/bash user

USER user
WORKDIR /home/user

ENV LANG C.UTF-8
