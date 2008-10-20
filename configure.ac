#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([gjs], [0.2], BUG-REPORT-ADDRESS)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([gjs/console.c])
AC_CONFIG_HEADER([config.h])

GETTEXT_PACKAGE=gjs
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [The name of the gettext domain])

AM_MAINTAINER_MODE

AC_PROG_CC
AM_PROG_CC_C_O
AC_ISC_POSIX
AC_HEADER_STDC

# no stupid static libraries
AM_DISABLE_STATIC
# avoid libtool for LTCOMPILE, use it only to link
AC_PROG_LIBTOOL
dnl DOLT

# Checks for libraries.
m4_define(gobject_required_version, 2.16.0)

## spidermonkey .pc file name varies across distributions
PKG_CHECK_EXISTS([firefox-js], [JS_PACKAGE=firefox-js],
                 [PKG_CHECK_EXISTS([xulrunner-js], [JS_PACKAGE=xulrunner-js], [JS_PACKAGE=mozilla-js])])

PKG_CHECK_MODULES(JS, $JS_PACKAGE)
AC_SUBST(JS_PACKAGE)
AC_CHECK_LIB([mozjs], [JS_CallTracer], :,
             [AC_MSG_ERROR([SpiderMonkey is too old, Firefox 3 is required])],
             [$JS_LIBS])

gjs_packages="gmodule-2.0 gobject-2.0 >= gobject_required_version $JS_PACKAGE"
gjstests_packages="$gjstests_packages $gjs_packages"
PKG_CHECK_MODULES([GJS], [$gjs_packages])

## some flavors of Firefox .pc only set sdkdir, not libdir
FIREFOX_JS_SDKDIR=`$PKG_CONFIG --variable=sdkdir $JS_PACKAGE`
FIREFOX_JS_LIBDIR=`$PKG_CONFIG --variable=libdir $JS_PACKAGE`

## Ubuntu does not set libdir in mozilla-js.pc
if test x"$FIREFOX_JS_LIBDIR" = x ; then
   ## Ubuntu returns xulrunner-devel as the sdkdir, but for the
   ## libdir we want the runtime location on the target system,
   ## so can't use -devel.
   ## The library is in the non-devel directory also.
   ## Don't ask me why it's in two places.
   FIREFOX_JS_LIBDIR=`echo "$FIREFOX_JS_SDKDIR" | sed -e 's/-devel//g'`

   if ! test -d "$FIREFOX_JS_LIBDIR" ; then
      FIREFOX_JS_LIBDIR=
   fi
fi

if test x"$FIREFOX_JS_LIBDIR" = x ; then
   AC_MSG_ERROR([Could not figure out where Firefox JavaScript library lives])
fi

AC_SUBST(FIREFOX_JS_LIBDIR)

## workaround for Ubuntu Hardy bug where mozilla-js.pc gives CFLAGS
## -I.../stable while jsapi.h is in .../unstable
js_include_dir=`$PKG_CONFIG --variable=includedir $JS_PACKAGE`/unstable
GJS_CFLAGS="$GJS_CFLAGS -I$js_include_dir"
AC_SUBST([js_include_dir])


PKG_CHECK_MODULES([GOBJECT_INTROSPECTION], [gobject-introspection-1.0])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

gjsjsdir="\${datadir}/gjs-1.0"
gjsnativedir="\${libdir}/gjs-1.0"
AC_SUBST([gjsjsdir])
AC_SUBST([gjsnativedir])

# gjs-tests links against everything
PKG_CHECK_MODULES([GJSTESTS], [$gjstests_packages])
GJSTESTS_CFLAGS="$GJSTESTS_CFLAGS -I$js_include_dir"

AC_CONFIG_FILES([Makefile gjs-1.0.pc gjs-gi-1.0.pc])
AC_OUTPUT
