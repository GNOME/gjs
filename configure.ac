#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

m4_define(pkg_major_version, 1)
m4_define(pkg_minor_version, 46)
m4_define(pkg_micro_version, 0)
m4_define(pkg_version, pkg_major_version.pkg_minor_version.pkg_micro_version)
m4_define(pkg_int_version, (pkg_major_version * 100 + pkg_minor_version) * 100 + pkg_micro_version)

AC_PREREQ([2.64])
AC_INIT([gjs],[pkg_version],[http://bugzilla.gnome.org/enter_bug.cgi?product=gjs],[gjs],[https://wiki.gnome.org/Projects/Gjs])
AM_INIT_AUTOMAKE([1.11.1 subdir-objects dist-xz no-dist-gzip tar-ustar -Wno-portability])
AX_IS_RELEASE([git-directory])
AC_CONFIG_SRCDIR([gjs/console.cpp])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_DEFINE([GJS_VERSION], pkg_int_version, [The gjs version as an integer])

AC_SUBST([GETTEXT_PACKAGE], [gjs])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [The name of the gettext domain])

AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

# our first pkg-config invocation is conditional, ensure macros still work
PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_11

LT_PREREQ([2.2.0])
# no stupid static libraries
LT_INIT([disable-static])

# Other programs
AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_SED

GNOME_CXX_WARNINGS([maximum])

AX_CODE_COVERAGE
dnl Clang does not need to link with -lgcov
AX_CHECK_LINK_FLAG([-lgcov],, [
	CODE_COVERAGE_LIBS=
	CODE_COVERAGE_LDFLAGS=
])

# Checks for libraries.
m4_define(glib_required_version, 2.50.0)

GOBJECT_INTROSPECTION_REQUIRE([1.41.4])

common_packages="gmodule-2.0 gthread-2.0 gio-2.0 >= glib_required_version mozjs-24"
gjs_packages="gobject-introspection-1.0 libffi $common_packages"
gjs_cairo_packages="cairo cairo-gobject $common_packages"
gjs_gdbus_packages="gobject-2.0 >= glib_required_version gio-2.0"
gjs_gtk_packages="gtk+-3.0 >= 3.20"
# gjs-tests links against everything
gjstests_packages="gio-unix-2.0 $gjs_packages"

PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= glib_required_version])
PKG_CHECK_MODULES([GJS], [$gjs_packages])
PKG_CHECK_MODULES([GJS_GDBUS], [$gjs_gdbus_packages])
PKG_CHECK_MODULES([GJSTESTS], [$gjstests_packages])

# Optional cairo dep (enabled by default)
AC_ARG_WITH(cairo,
  [AS_HELP_STRING([--without-cairo], [Don't build cairo module])])
AS_IF([test "x$with_cairo" != "xno"],
  [PKG_CHECK_MODULES([GJS_CAIRO], [$gjs_cairo_packages],
    [have_cairo=yes], [have_cairo=no])],
  [have_cairo=no])
AM_CONDITIONAL(ENABLE_CAIRO, test x$have_cairo = xyes)
AS_IF([test x$have_cairo = xyes], [
  AC_DEFINE([ENABLE_CAIRO],[1],[Define if you want to build with cairo support])
  PKG_CHECK_MODULES([GJS_CAIRO_XLIB], [cairo-xlib], [],
    [AC_MSG_WARN([Cairo-xlib support not found])])
], [AS_IF([test "x$with_cairo" = "xyes"],
  [AC_MSG_ERROR([Cairo requested but not found])])])

# Optional GTK+ dep (enabled by default)
AC_ARG_WITH(gtk,
  [AS_HELP_STRING([--without-gtk], [Don't build GTK-related code])])
AS_IF([test "x$with_gtk" != "xno"],
  [PKG_CHECK_MODULES([GJS_GTK], [$gjs_gtk_packages],
    [have_gtk=yes], [have_gtk=no])],
  [have_gtk=no])
AM_CONDITIONAL(ENABLE_GTK, test x$have_gtk = xyes)
AS_IF([test x$have_gtk = xyes], [
  AC_DEFINE([ENABLE_GTK],[1],[Define if you want to build with GTK+ support])
], [AS_IF([test "x$with_gtk" = "xyes"],
  [AC_MSG_ERROR([GTK requested but not found])])])

PKG_CHECK_VAR([GI_DATADIR], [gobject-introspection-1.0], [gidatadir])

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
  ac_cv_header_readline_readline_h=no
else
  AC_MSG_RESULT([$READLINE_LIBS])
  AC_CHECK_HEADERS([readline/readline.h])
fi

AC_SUBST([READLINE_LIBS])

# End of readline checks: restore LIBS
LIBS=$LIBS_no_readline

AC_CHECK_FUNCS([backtrace])

AC_ARG_ENABLE(installed_tests,
  [AS_HELP_STRING([--enable-installed-tests],
    [Install test programs @<:@default: no@:>@])])
AM_CONDITIONAL(BUILDOPT_INSTALL_TESTS, test x$enable_installed_tests = xyes)

dnl
dnl Tracing
dnl

AC_ARG_ENABLE([dtrace],
  [AS_HELP_STRING([--enable-dtrace],
    [Include dtrace trace support @<:@default: no@:>@])])

AC_ARG_ENABLE([systemtap],
  [AS_HELP_STRING([--enable-systemtap],
    [Include systemtap trace support (implies --enable-dtrace) @<:@default: no@:>@])])
AS_IF([test "x$enable_systemtap" = "xyes"], [enable_dtrace=yes])
AM_CONDITIONAL([ENABLE_SYSTEMTAP], [test "x$enable_systemtap" = "xyes"])

AS_IF([test "x$enable_dtrace" = "xyes"], [
  AC_PATH_PROG([DTRACE], [dtrace])
  AS_IF([test -z "$DTRACE"],
    [AC_MSG_ERROR([dtrace is required for --enable-dtrace or --enable-systemtap])])
  AC_CHECK_HEADER([sys/sdt.h],,
    [AC_MSG_ERROR([sys/sdt.h header is required for --enable-dtrace or --enable-systemtap])])
  AC_DEFINE([HAVE_DTRACE], [1], [Define to 1 if using dtrace probes.])
])
AM_CONDITIONAL([ENABLE_DTRACE], [test "x$enable_dtrace" = "xyes"])

dnl
dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
dnl
AC_ARG_ENABLE([Bsymbolic],
  [AS_HELP_STRING([--disable-Bsymbolic], [avoid linking with -Bsymbolic])])
AS_IF([test "x$enable_Bsymbolic" != "xno"],
  [AX_APPEND_LINK_FLAGS([-Bsymbolic-functions])])

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

AC_SUBST([gjsjsdir], [\${datadir}/gjs-1.0])

dnl automake 1.11/1.12 defines this but does not substitute it
AC_SUBST([pkglibexecdir], ["${libexecdir}/${PACKAGE}"])

AC_CONFIG_FILES([Makefile gjs-1.0.pc gjs-internals-1.0.pc])
dnl Symlink the files from gobject-introspection's test libraries into the tree;
dnl Automake plans to drop support for compiling them in-place.
AC_CONFIG_LINKS([
  installed-tests/gitestmacros.h:$GI_DATADIR/tests/gitestmacros.h
  installed-tests/regress.c:$GI_DATADIR/tests/regress.c
  installed-tests/regress.h:$GI_DATADIR/tests/regress.h
  installed-tests/warnlib.c:$GI_DATADIR/tests/warnlib.c
  installed-tests/warnlib.h:$GI_DATADIR/tests/warnlib.h
  installed-tests/gimarshallingtests.c:$GI_DATADIR/tests/gimarshallingtests.c
  installed-tests/gimarshallingtests.h:$GI_DATADIR/tests/gimarshallingtests.h
])
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
	readline:		${ac_cv_header_readline_readline_h}
	dtrace:			${enable_dtrace:-no}
	systemtap:		${enable_systemtap:-no}
	Run tests under:	${TEST_MSG}
	Code coverage:		${enable_code_coverage}
])
