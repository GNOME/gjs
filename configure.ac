#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([gjs], [0.7.8],[http://bugzilla.gnome.org/enter_bug.cgi?product=gjs])
AM_INIT_AUTOMAKE([dist-bzip2 no-dist-gzip])
AC_CONFIG_SRCDIR([gjs/console.c])
AC_CONFIG_HEADER([config.h])

GETTEXT_PACKAGE=gjs
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [The name of the gettext domain])

AM_MAINTAINER_MODE
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

# Add extra warning flags
changequote(,)dnl
ensureflag() {
  flag="$1"; shift
  result="$@"

  case " ${result} " in
  *[\ \	]${flag}[\ \	]*) ;;
  *) result="${flag} ${result}" ;;
  esac

  echo ${result}
}
changequote([,])dnl

if test "$GCC" = "yes"; then
    for flag in -Wall -Wchar-subscripts -Wmissing-declarations \
        -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align \
        -Wsign-compare;
    do
        CFLAGS="`ensureflag $flag $CFLAGS`"
        CXXFLAGS="`ensureflag $flag $CXXFLAGS`"
    done
fi

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

    CFLAGS="$CFLAGS -g -O0 -fprofile-arcs -ftest-coverage"
    CXXFLAGS="$CXXFLAGS -g -O0 -fprofile-arcs -ftest-coverage"
fi
AM_CONDITIONAL([ENABLE_COVERAGE], [test x$enable_coverage = xyes])

# Checks for libraries.
m4_define(gobject_required_version, 2.18.0)

# Look for Spidermonkey. If js-config exists, use that;
# otherwise we try some pkgconfig files from various distributions.

AC_ARG_VAR([JS_CONFIG], [The js-config program to use])
if test "$ac_cv_env_JS_CONFIG_set" != "set"; then
    AC_PATH_PROG([JS_CONFIG], [js-config], [])
fi

if test -n "$JS_CONFIG"; then
    JS_CFLAGS="$($JS_CONFIG --cflags)"
    JS_LIBS="$($JS_CONFIG --libs)"
    FIREFOX_JS_LIBDIR="$($JS_CONFIG --libdir)"
    JS_PACKAGE=

    # js-config gives almost usable CFLAGS, we must define one of XP_BEOS,
    # XP_OS2, XP_WIN or XP_UNIX
    JS_CFLAGS="$JS_CFLAGS -DXP_UNIX"
else
    ## spidermonkey .pc file name varies across distributions and Gecko version
    ##
    ## mozilla-js: Gecko 1.9, all distributions
    ## xulrunner-js: Gecko 1.8 (and earlier?) Debian/Ubuntu
    ## firefox-js: ???
    ##
    ## Checking for mozilla-js first will hopefully get us the newest version
    ## of spidermonkey.
    PKG_CHECK_EXISTS([mozilla-js], [JS_PACKAGE=mozilla-js],
                     [PKG_CHECK_EXISTS([xulrunner-js], [JS_PACKAGE=xulrunner-js], [JS_PACKAGE=firefox-js])])

    PKG_CHECK_MODULES(JS, $JS_PACKAGE)

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
fi

if test x"$FIREFOX_JS_LIBDIR" = x ; then
   AC_MSG_ERROR([Could not figure out where Firefox JavaScript library lives])
fi

AC_SUBST(JS_PACKAGE)
AC_SUBST(FIREFOX_JS_LIBDIR)

AC_MSG_CHECKING([for mozilla-js >= 1.9.2 ])
if `$PKG_CONFIG --exists $JS_PACKAGE '>=' 1.9.2`; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_ERROR([$JS_PACKAGE >= 1.9.2 is required])
fi

AC_CHECK_LIB([mozjs], [JS_GetStringBytes], AC_DEFINE([HAVE_JS_GETSTRINGBYTES], [1], [Define if we still have JS_GetStringBytes]),, [$JS_LIBS])

AC_MSG_CHECKING([for mozilla-js >= 2 ])
if `$PKG_CONFIG --exists $JS_PACKAGE '>=' 2`; then
  AC_MSG_RESULT([yes])
  AC_DEFINE(HAVE_MOZJS_2, [1], [Define if mozilla-js is at least 2])
  AC_CHECK_LIB([mozjs], [JS_FreezeObject], AC_DEFINE([HAVE_JS_FREEZEOBJECT], [1], [Define if we have JS_FreezeObject]),
              , [$JS_LIBS])
  AC_CHECK_LIB([mozjs], [JS_IsScriptFrame], AC_DEFINE([HAVE_JS_ISSCRIPTFRAME], [1], [Define if we have JS_IsScriptFrame]),
              , [$JS_LIBS])
  AC_CHECK_LIB([mozjs], [JS_EndPC], AC_DEFINE([HAVE_JS_ENDPC], [1], [Define if we have JS_EndPC]),
              , [$JS_LIBS])

else
  AC_MSG_RESULT([no])
fi

## workaround for Ubuntu Hardy bug where mozilla-js.pc gives CFLAGS
## -I.../stable while jsapi.h is in .../unstable
AC_MSG_CHECKING([if SpiderMonkey needs extra compiler flags])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $JS_CFLAGS"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <jsapi.h>]])],
    [js_extra_cflags_needed=no],
    [js_extra_cflags_needed=yes])
CFLAGS="$save_CFLAGS"
AC_MSG_RESULT([$js_extra_cflags_needed])

JS_EXTRA_CFLAGS=
if test "$js_extra_cflags_needed" = yes; then
    try_cflags="-I`$PKG_CONFIG --variable=includedir $JS_PACKAGE`/unstable"
    AC_MSG_CHECKING([if $try_cflags works])
    save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $JS_CFLAGS $try_cflags"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <jsapi.h>]])],
        [AC_MSG_RESULT([yes])
         JS_EXTRA_CFLAGS="$try_cflags"],
        [AC_MSG_RESULT([no])])
    CFLAGS="$save_CFLAGS"

    if test x"$JS_EXTRA_CFLAGS" = x; then
        AC_MSG_ERROR([Unable to determine extra compiler flags needed])
    fi
fi
AC_SUBST([JS_EXTRA_CFLAGS])

common_packages="gobject-2.0 >= gobject_required_version $JS_PACKAGE"
gjs_packages="gmodule-2.0 gthread-2.0 $common_packages"
gjs_gi_packages="gobject-introspection-1.0 >= 0.9.5 $common_packages"
gjs_cairo_gobject_packages="cairo-gobject $common_packages"
gjs_cairo_packages="cairo $common_packages"
gjs_dbus_packages="dbus-glib-1 $common_packages"
# gjs-tests links against everything
gjstests_packages="$gjstests_packages $gjs_packages"

PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= gobject_required_version])
PKG_CHECK_MODULES([GJS], [$gjs_packages])
PKG_CHECK_MODULES([GJS_GI], [$gjs_gi_packages])
# Prefer cairo-gobject if we have it
PKG_CHECK_MODULES([GJS_CAIRO], [$gjs_cairo_gobject_packages], have_cairo_gobject=yes, have_cairo_gobject=no)
if test x${have_cairo_gobject} != xyes; then
  PKG_CHECK_MODULES([GJS_CAIRO], [$gjs_cairo_packages])
fi
PKG_CHECK_MODULES([GJS_DBUS], [$gjs_dbus_packages])
saved_CFLAGS=$CFLAGS
CFLAGS=$GJS_DBUS_CFLAGS
saved_LIBS=$LIBS
LIBS=$GJS_DBUS_LIBS
AC_CHECK_FUNCS(dbus_message_iter_abandon_container)
CFLAGS=$saved_CFLAGS
LIBS=$saved_LIBS
PKG_CHECK_MODULES([GJSTESTS], [$gjstests_packages])

GI_DATADIR=$($PKG_CONFIG --variable=gidatadir gobject-introspection-1.0)
AC_SUBST(GI_DATADIR)

if test x"$JS_PACKAGE" = x; then
    # if JS_PACKAGE is undefined, pkg-config modules don't include
    # SpiderMonkey and we need to deal with it ourselves
    GJS_CFLAGS="$GJS_CFLAGS $JS_CFLAGS"
    GJS_LIBS="$GJS_LIBS $JS_LIBS"
    GJS_GI_CFLAGS="$GJS_GI_CFLAGS $JS_CFLAGS"
    GJS_GI_LIBS="$GJS_GI_LIBS $JS_LIBS"
    GJS_CAIRO_CFLAGS="$GJS_CAIRO_CFLAGS $JS_CFLAGS"
    GJS_CAIRO_LIBS="$GJS_CAIRO_LIBS $JS_LIBS"
    GJS_DBUS_CFLAGS="$GJS_DBUS_CFLAGS $JS_CFLAGS"
    GJS_DBUS_LIBS="$GJS_DBUS_LIBS $JS_LIBS"
    GJSTESTS_CFLAGS="$GJSTESTS_CFLAGS $JS_CFLAGS"
    GJSTESTS_LIBS="$GJSTEST_LIBS $JS_LIBS"
fi

GJS_CFLAGS="$GJS_CFLAGS $JS_EXTRA_CFLAGS"
GJS_GI_CFLAGS="$GJS_GI_CFLAGS $JS_EXTRA_CFLAGS"
GJS_CAIRO_CFLAGS="$GJS_CAIRO_CFLAGS $JS_EXTRA_CFLAGS"
GJS_DBUS_CFLAGS="$GJS_DBUS_CFLAGS $JS_EXTRA_CFLAGS"
GJSTESTS_CFLAGS="$GJSTESTS_CFLAGS $JS_EXTRA_CFLAGS"

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

gjsjsdir="\${datadir}/gjs-1.0"
gjsnativedir="\${libdir}/gjs-1.0"
AC_SUBST([gjsjsdir])
AC_SUBST([gjsnativedir])

AC_CONFIG_FILES([Makefile gjs-1.0.pc gjs-gi-1.0.pc gjs-dbus-1.0.pc gjs-internals-1.0.pc])
AC_OUTPUT
