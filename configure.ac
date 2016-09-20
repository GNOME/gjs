#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

m4_define(pkg_major_version, 1)
m4_define(pkg_minor_version, 46)
m4_define(pkg_micro_version, 0)
m4_define(pkg_version, pkg_major_version.pkg_minor_version.pkg_micro_version)
m4_define(pkg_int_version, (pkg_major_version * 100 + pkg_minor_version) * 100 + pkg_micro_version)

AC_PREREQ(2.61)
AC_INIT([gjs], pkg_version, [http://bugzilla.gnome.org/enter_bug.cgi?product=gjs])
AM_INIT_AUTOMAKE([dist-xz no-dist-gzip tar-ustar])
AC_CONFIG_SRCDIR([gjs/console.cpp])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])

AC_DEFINE([GJS_VERSION], pkg_int_version, [The gjs version as an integer])

GETTEXT_PACKAGE=gjs
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [The name of the gettext domain])

AM_MAINTAINER_MODE([enable])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# our first pkg-config invocation is conditional, ensure macros still work
PKG_PROG_PKG_CONFIG

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_ISC_POSIX
AC_HEADER_STDC

# no stupid static libraries
AM_DISABLE_STATIC
# avoid libtool for LTCOMPILE, use it only to link
AC_PROG_LIBTOOL
dnl DOLT

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

GNOME_CXX_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

# coverage
AC_ARG_ENABLE([coverage],
              [AS_HELP_STRING([--enable-coverage],
                              [enable code coverage])],
              , [enable_coverage=no])
if test x$enable_coverage = xyes; then
    if test x$GCC != xyes; then
        AC_MSG_ERROR([GCC is required for --enable-coverage])
    fi

    AC_PATH_TOOL([LCOV], [lcov])
    AC_PATH_TOOL([GENHTML], [genhtml])

    if test x$LCOV = x || test x$GENHTML = x; then
        AC_MSG_ERROR([lcov and genhtml are required for --enable-coverage])
    fi

    CFLAGS="$CFLAGS -g -O0 --coverage"
    CXXFLAGS="$CXXFLAGS -g -O0 --coverage"
    LIBS="$LIBS -lgcov"
fi
AM_CONDITIONAL([ENABLE_COVERAGE], [test x$enable_coverage = xyes])

# Checks for libraries.
m4_define(glib_required_version, 2.42.0)

AC_CHECK_HEADERS([malloc.h])
AC_CHECK_FUNCS(mallinfo)

GOBJECT_INTROSPECTION_REQUIRE([1.41.4])

common_packages="gmodule-2.0 gthread-2.0 gio-2.0 >= glib_required_version mozjs-24"
gjs_packages="gobject-introspection-1.0 libffi $common_packages"
gjs_cairo_packages="cairo cairo-gobject $common_packages"
gjs_gdbus_packages="gobject-2.0 >= glib_required_version gio-2.0"
gjs_gtk_packages="gtk+-3.0"
# gjs-tests links against everything
gjstests_packages="gio-unix-2.0 $gjs_packages"

PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= glib_required_version])
PKG_CHECK_MODULES([GJS], [$gjs_packages])
PKG_CHECK_MODULES([GJS_GDBUS], [$gjs_gdbus_packages])
PKG_CHECK_MODULES([GJSTESTS], [$gjstests_packages])

# Optional cairo dep (enabled by default)
AC_ARG_WITH(cairo,
	    AS_HELP_STRING([--without-cairo], [Use cairo @<:@default=yes@:>@]),
	    [], [with_cairo=yes])
AS_IF([test x$with_cairo = xyes], [
  PKG_CHECK_MODULES([GJS_CAIRO], [$gjs_cairo_packages], have_cairo=yes, have_cairo=no)
  ])
AM_CONDITIONAL(ENABLE_CAIRO, test x$have_cairo = xyes)
AS_IF([test x$have_cairo = xyes], [
  AC_DEFINE([ENABLE_CAIRO],[1],[Define if you want to build with cairo support])
])
PKG_CHECK_MODULES([GJS_CAIRO_XLIB], [cairo-xlib], [],
  [AC_MSG_WARN([Cairo-xlib support not found])])

# Optional GTK+ dep (enabled by default)
AC_ARG_WITH(gtk,
            AS_HELP_STRING([--without-gtk], [Use GTK+ @<:@default=yes@:>@]),
            [], [with_gtk=yes])
AS_IF([test x$with_gtk = xyes], [
  PKG_CHECK_MODULES([GJS_GTK], [$gjs_gtk_packages], have_gtk=yes, have_gtk=no)
  ], [have_gtk=no])
AM_CONDITIONAL(ENABLE_GTK, test x$have_gtk = xyes)
AS_IF([test x$have_gtk = xyes], [
  AC_DEFINE([ENABLE_GTK],[1],[Define if you want to build with GTK+ support])
])

GI_DATADIR=$($PKG_CONFIG --variable=gidatadir gobject-introspection-1.0)
AC_SUBST(GI_DATADIR)

# readline
LIBS_no_readline=$LIBS

# On some systems we need to link readline to a termcap compatible
# library.
gjs_cv_lib_readline=no
AC_MSG_CHECKING([how to link readline libs])
for gjs_libtermcap in "" ncursesw ncurses curses termcap; do
  if test -z "$gjs_libtermcap"; then
    READLINE_LIBS="-lreadline"
  else
    READLINE_LIBS="-lreadline -l$gjs_libtermcap"
  fi
  LIBS="$READLINE_LIBS $LIBS_no_readline"
  AC_LINK_IFELSE(
    [AC_LANG_CALL([],[readline])],
    [gjs_cv_lib_readline=yes])
  if test $gjs_cv_lib_readline = yes; then
    break
  fi
done
if test $gjs_cv_lib_readline = no; then
  AC_MSG_RESULT([none])
  READLINE_LIBS=""
else
  AC_MSG_RESULT([$READLINE_LIBS])
  AC_DEFINE(HAVE_LIBREADLINE, 1,
    [Define if you have the readline library (-lreadline).])
fi

AC_TRY_CPP([#include <readline/readline.h>],
have_readline=yes, have_readline=no)

AM_CONDITIONAL([HAVE_READLINE], [test x$have_readline = xyes])
AC_SUBST([READLINE_LIBS])
AC_SUBST([HAVE_READLINE])

# End of readline checks: restore LIBS
LIBS=$LIBS_no_readline

AC_CHECK_FUNCS([backtrace])

AC_ARG_ENABLE(installed_tests,
              AS_HELP_STRING([--enable-installed-tests],
                             [Install test programs (default: no)]),,
              [enable_installed_tests=no])
AM_CONDITIONAL(BUILDOPT_INSTALL_TESTS, test x$enable_installed_tests = xyes)

dnl
dnl Tracing
dnl

AC_MSG_CHECKING([whether to include systemtap tracing support])
AC_ARG_ENABLE([systemtap],
             [AS_HELP_STRING([--enable-systemtap],
                              [Enable inclusion of systemtap trace support])],
              [ENABLE_SYSTEMTAP="${enableval}"], [ENABLE_SYSTEMTAP='no'])
AC_MSG_RESULT(${ENABLE_SYSTEMTAP})

AC_MSG_CHECKING([whether to include dtrace tracing support])
AC_ARG_ENABLE([dtrace],
             [AS_HELP_STRING([--enable-dtrace],
                              [Enable inclusion of dtrace trace support])],
              [ENABLE_DTRACE="${enableval}"], [ENABLE_DTRACE='no'])
AC_MSG_RESULT(${ENABLE_DTRACE})

AM_CONDITIONAL([ENABLE_SYSTEMTAP], [test x$ENABLE_SYSTEMTAP = xyes])
AM_CONDITIONAL([ENABLE_DTRACE], [test x$ENABLE_DTRACE = xyes -o x$ENABLE_SYSTEMTAP = xyes])

if test "x${ENABLE_DTRACE}" = xyes -o "x${ENABLE_SYSTEMTAP}" = xyes; then
  AC_CHECK_PROGS(DTRACE, dtrace)
  if test -z "$DTRACE"; then
    AC_MSG_ERROR([dtrace not found])
  fi
  AC_CHECK_HEADER([sys/sdt.h], [SDT_H_FOUND='yes'],
                  [SDT_H_FOUND='no';
                   AC_MSG_ERROR([tracing support needs sys/sdt.h header])])
  AC_DEFINE([HAVE_DTRACE], [1], [Define to 1 if using dtrace probes.])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl
dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
dnl
AC_ARG_ENABLE(Bsymbolic,
              [AS_HELP_STRING([--disable-Bsymbolic],
                              [avoid linking with -Bsymbolic])],,
              [SAVED_LDFLAGS="${LDFLAGS}"
               AC_MSG_CHECKING([for -Bsymbolic-functions linker flag])
               LDFLAGS=-Wl,-Bsymbolic-functions
               AC_TRY_LINK([], [int main (void) { return 0; }],
                           AC_MSG_RESULT(yes)
                           enable_Bsymbolic=yes,
                           AC_MSG_RESULT(no)
                           enable_Bsymbolic=no)
               LDFLAGS="${SAVED_LDFLAGS}"])

if test "x${enable_Bsymbolic}" = "xyes"; then
  EXTRA_LINK_FLAGS=-Wl,-Bsymbolic-functions
fi

AC_SUBST(EXTRA_LINK_FLAGS)

AC_ARG_WITH([xvfb-tests],
  [AS_HELP_STRING([--with-xvfb-tests],
    [Run all tests under an XVFB server @<:@default=no@:>@])])
AS_IF([test "x$with_xvfb_tests" = "xyes"], [
  AC_PATH_PROG([XVFB], [Xvfb])
  AS_IF([test -z "$XVFB"],
    [AC_MSG_ERROR([Xvfb is required for --with-xvfb-tests])])
])
AM_CONDITIONAL([XVFB_TESTS], [test "x$with_xvfb_tests" = "xyes"])

AC_ARG_WITH([dbus-tests],
  [AS_HELP_STRING([--without-dbus-tests],
    [Don't try to use DBus during make check @<:@default=use@:>@])])
AS_IF([test "x$with_dbus_tests" != "xno"], [
  AC_PATH_PROG([DBUS_UUIDGEN], [dbus-uuidgen])
  AS_IF([test -z "$DBUS_UUIDGEN"],
    [AC_MSG_ERROR([dbus-uuidgen is required for --with-dbus-tests])])
])
AM_CONDITIONAL([DBUS_TESTS], [test "x$with_dbus_tests" != "xno"])

gjsjsdir="\${datadir}/gjs-1.0"
AC_SUBST([gjsjsdir])

dnl automake 1.11/1.12 defines this but does not substitute it
AC_SUBST([pkglibexecdir], ["${libexecdir}/${PACKAGE}"])

AC_CONFIG_FILES([Makefile gjs-1.0.pc gjs-internals-1.0.pc])
AC_OUTPUT

TEST_MSG=
AM_COND_IF([XVFB_TESTS], [TEST_MSG="xvfb "])
AM_COND_IF([DBUS_TESTS], [TEST_MSG="${TEST_MSG}dbus"])
AS_IF([test -z "$TEST_MSG"], [TEST_MSG="nothing special"])

AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION

	GJS_CFLAGS:		${GJS_CFLAGS}
	GJS_LIBS:		${GJS_LIBS}

	cairo:			${have_cairo}
	GTK+:			${have_gtk}
	readline:		${have_readline}
	dtrace:			${ENABLE_DTRACE}
	systemtap:		${ENABLE_SYSTEMTAP}
	Run tests under:	${TEST_MSG}
])
