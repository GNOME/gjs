bin_PROGRAMS =
lib_LTLIBRARIES =
noinst_HEADERS =
noinst_LTLIBRARIES =
dist_gjsjs_DATA =
gjsnative_LTLIBRARIES =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST =
gjstest_files_with_tests =
TEST_PROGS =
check_PROGRAMS = $(TEST_PROGS)

gjsjsdir = @gjsjsdir@
gjstweenerdir = @gjsjsdir@/tweener
gjsnativedir = @gjsnativedir@

gjsincludedir = $(includedir)/gjs-1.0

########################################################################
nobase_gjsinclude_HEADERS =	\
	gjs/context.h		\
	gjs/importer.h		\
	gjs/gjs.h		\
	gjs/jsapi-util.h	\
	gjs/mem.h		\
	gjs/native.h
noinst_HEADERS +=		\
	gjs/context-jsapi.h	\
	gjs/profiler.h		\
	util/crash.h		\
	util/error.h		\
	util/glib.h		\
	util/log.h		\
	util/misc.h

########################################################################
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gjs-1.0.pc

EXTRA_DIST += 			\
	gjs-1.0.pc.in

########################################################################
gjs_directory_defines = 				\
	-DGJS_TOP_SRCDIR=\"$(top_srcdir)\"		\
	-DGJS_JS_DIR=\"$(gjsjsdir)\"			\
	-DGJS_NATIVE_DIR=\"$(gjsnativedir)\"

########################################################################
lib_LTLIBRARIES += libgjs.la

libgjs_la_CPPFLAGS =		\
	$(AM_CPPFLAGS)		\
	$(gjs_directory_defines)
libgjs_la_CFLAGS = 		\
	$(AM_CFLAGS)		\
	$(GJS_CFLAGS)
libgjs_la_LDFLAGS = 		\
	-export-symbols-regex "^[^_]" -version-info 0:0:0	\
	-R $(FIREFOX_JS_LIBDIR) -rdynamic
libgjs_la_LIBADD = 		\
	$(GJS_LIBS)

libgjs_la_SOURCES =		\
	gjs/context.c		\
	gjs/importer.c		\
	gjs/jsapi-util.c		\
	gjs/jsapi-util-array.c	\
	gjs/jsapi-util-error.c	\
	gjs/jsapi-util-string.c	\
	gjs/mem.c		\
	gjs/native.c		\
	gjs/profiler.c		\
	gjs/stack.c		\
	util/error.c		\
	util/glib.c		\
	util/crash.c		\
	util/log.c		\
	util/misc.c

gjstest_files_with_tests += 	\
	gjs/jsapi-util-array.c	\
	gjs/jsapi-util-error.c	\
	gjs/jsapi-util-string.c	\
	gjs/stack.c				\
	util/glib.c

include Makefile-gjs-dbus.am
include Makefile-gi.am
include Makefile-modules.am
include Makefile-examples.am
########################################################################
bin_PROGRAMS += gjs-console

gjs_console_CFLAGS = 		\
	$(AM_CFLAGS)		\
	$(GJS_CFLAGS)
gjs_console_LDADD =		\
         libgjs.la

gjs_console_SOURCES = gjs/console.c

install-exec-hook:
	(cd $(DESTDIR)$(bindir) && ln -sf gjs-console$(EXEEXT) gjs$(EXEEXT))

include Makefile-test.am
