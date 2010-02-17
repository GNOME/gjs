#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([gjs], [0.5],[http://bugzilla.gnome.org/enter_bug.cgi?product=gjs])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([gjs/console.c])
AC_CONFIG_HEADER([config.h])

GETTEXT_PACKAGE=gjs
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [The name of the gettext domain])

AM_MAINTAINER_MODE

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
        -Wsign-compare  -Wfloat-equal;
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
m4_define(gobject_required_version, 2.16.0)

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

AC_CHECK_LIB([mozjs], [JS_CallTracer], :,
             [AC_MSG_ERROR([SpiderMonkey is too old, Firefox 3 is required])],
             [$JS_LIBS])

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

# Check that SpiderMonkey is compiled with --enable-threadsafe, since
# GJS needs JS_GetClass etc. to take a JSContext* argument.
#
# NOTE: This does not imply that GJS itself is thread-safe! (It isn't.)
AC_MSG_CHECKING([if JS_GetClass is compatible])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $JS_CFLAGS $JS_EXTRA_CFLAGS"
AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <jsapi.h>]], [[
JSContext *cx = NULL;
JSObject *obj = NULL;
(void)JS_GetClass(cx, obj);
                     ]])],
    [js_getclass=yes],
    [js_getclass=no])
CFLAGS="$save_CFLAGS"
AC_MSG_RESULT([$js_getclass])

if test "$js_getclass" = no; then
    AC_MSG_ERROR([SpiderMonkey needs to be compiled with --enable-threadsafe])
fi

common_packages="gobject-2.0 >= gobject_required_version $JS_PACKAGE"
gjs_packages="gmodule-2.0 gthread-2.0 $common_packages"
gjs_gi_packages="gobject-introspection-1.0 >= 0.6.3 $common_packages"
gjs_cairo_packages="cairo $common_packages"
gjs_dbus_packages="dbus-glib-1 $common_packages"
# gjs-tests links against everything
gjstests_packages="$gjstests_packages $gjs_packages"

PKG_CHECK_MODULES([GJS], [$gjs_packages])
PKG_CHECK_MODULES([GJS_GI], [$gjs_gi_packages])
PKG_CHECK_MODULES([GJS_CAIRO], [$gjs_cairo_packages])
PKG_CHECK_MODULES([GJS_DBUS], [$gjs_dbus_packages])
PKG_CHECK_MODULES([GJSTESTS], [$gjstests_packages])

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

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

gjsjsdir="\${datadir}/gjs-1.0"
gjsnativedir="\${libdir}/gjs-1.0"
AC_SUBST([gjsjsdir])
AC_SUBST([gjsnativedir])

AC_CONFIG_FILES([Makefile gjs-1.0.pc gjs-gi-1.0.pc gjs-dbus-1.0.pc])
AC_OUTPUT
