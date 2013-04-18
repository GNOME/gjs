#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([gjs-installed-tests], 1.36.0, [http://bugzilla.gnome.org/enter_bug.cgi?product=gjs])
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip foreign])
AC_CONFIG_SRCDIR([autogen.sh])
AC_CONFIG_HEADER([config.h])

AM_MAINTAINER_MODE([enable])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# our first pkg-config invocation is conditional, ensure macros still work
PKG_PROG_PKG_CONFIG

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AM_DISABLE_STATIC

GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

PKG_CHECK_MODULES([GJS], [gio-unix-2.0 >= 2.36 gjs-internals-1.0 >= 1.36.0])

GOBJECT_INTROSPECTION_REQUIRE([1.36.0])
GI_DATADIR=$($PKG_CONFIG --variable=gidatadir gobject-introspection-1.0)
AC_SUBST(GI_DATADIR)

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

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION

	GJS_CFLAGS:		${GJS_CFLAGS}
	GJS_LIBS:		${GJS_LIBS}
])
