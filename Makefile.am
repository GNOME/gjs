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
gjsoverridedir = $(gjsjsdir)/overrides
gjstweenerdir = $(gjsjsdir)/tweener
gjsnativedir = @gjsnativedir@

gjs_public_includedir = $(includedir)/gjs-1.0
gjs_module_includedir = $(includedir)/gjs-1.0

########################################################################
nobase_gjs_public_include_HEADERS =	\
	gjs/context.h		\
	gjs/gjs.h

nobase_gjs_module_include_HEADERS =	\
	gjs/gjs-module.h	\
	gjs/compat.h		\
	gjs/byteArray.h		\
	gjs/importer.h		\
	gjs/jsapi-util.h	\
	gjs/mem.h		\
	gjs/native.h

noinst_HEADERS +=		\
	gjs/jsapi-private.h	\
	gjs/profiler.h		\
	gjs/unit-test-utils.h	\
	util/crash.h		\
	util/error.h		\
	util/glib.h		\
	util/log.h		\
	util/misc.h

########################################################################
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gjs-1.0.pc gjs-internals-1.0.pc

EXTRA_DIST += 			\
	gjs-1.0.pc.in           \
	gjs-internals-1.0.pc.in

########################################################################
gjs_directory_defines = 				\
	-DGJS_TOP_SRCDIR=\"$(top_srcdir)\"		\
	-DGJS_JS_DIR=\"$(gjsjsdir)\"			\
	-DGJS_NATIVE_DIR=\"$(gjsnativedir)\"

########################################################################
lib_LTLIBRARIES += libgjs.la

libgjs_la_CPPFLAGS =		\
	$(AM_CPPFLAGS)		\
	$(GJS_CFLAGS)		\
	$(gjs_directory_defines)\
	-DGJS_COMPILATION
libgjs_la_CFLAGS = 		\
	$(AM_CFLAGS)
libgjs_la_LDFLAGS = 		\
	-export-symbols-regex "^[^_]" -version-info 0:0:0	\
	-no-undefined \
	-rdynamic
if HAVE_FIREFOX_JS_LIBDIR
libgjs_la_LDFLAGS += -R $(FIREFOX_JS_LIBDIR) 
endif
libgjs_la_LIBADD = 		\
	$(GJS_LIBS)

libgjs_la_SOURCES =		\
	gjs/byteArray.c		\
	gjs/context.c		\
	gjs/importer.c		\
	gjs/jsapi-private.cpp	\
	gjs/jsapi-util.c	\
	gjs/jsapi-util-array.c	\
	gjs/jsapi-util-error.c	\
	gjs/jsapi-util-string.c	\
	gjs/mem.c		\
	gjs/native.c		\
	gjs/profiler.c		\
	gjs/stack.c		\
	gjs/unit-test-utils.c	\
	util/error.c		\
	util/glib.c		\
	util/crash.c		\
	util/log.c		\
	util/misc.c

gjstest_files_with_tests += 	\
	gjs/context.c		\
	gjs/jsapi-util-array.c	\
	gjs/jsapi-util-error.c	\
	gjs/jsapi-util-string.c	\
	gjs/stack.c				\
	util/glib.c

tapset_in_files = gjs/gjs.stp.in
EXTRA_DIST += $(tapset_in_files)
if ENABLE_SYSTEMTAP
gjs/gjs.stp: gjs/gjs.stp.in Makefile
	sed -e s,@EXPANDED_LIBDIR@,$(libdir), < $< > $@.tmp && mv $@.tmp $@
tapsetdir   = $(DESTDIR)$(datadir)/systemtap/tapset
tapset_DATA = $(tapset_in_files:.stp.in=.stp)
endif

include Makefile-gjs-dbus.am
include Makefile-gi.am
include Makefile-modules.am
include Makefile-examples.am
########################################################################
bin_PROGRAMS += gjs-console

gjs_console_CFLAGS = 		\
	$(AM_CFLAGS)		\
        $(GOBJECT_CFLAGS)
gjs_console_LDADD =		\
	$(JS_LIBS)		\
	$(GOBJECT_LIBS)		\
	libgjs.la
gjs_console_LDFLAGS = -rdynamic
if HAVE_FIREFOX_JS_LIBDIR
gjs_console_LDFLAGS += -R $(FIREFOX_JS_LIBDIR)
endif
gjs_console_SOURCES = gjs/console.c

install-exec-hook:
	(cd $(DESTDIR)$(bindir) && ln -sf gjs-console$(EXEEXT) gjs$(EXEEXT))

include Makefile-test.am

prepare-release-tag:
	TAGNAME=`echo $(PACKAGE) | tr - _ | tr a-z A-Z` \
	TAGVER=`echo $(VERSION) | sed s/\\\./_/g`; \
	  git tag -m "Tag $(VERSION)" -a "$$TAGNAME"_"$$TAGVER"

prepare-minor-release: $(distdir).tar.bz2 prepare-release-tag
	python $(top_srcdir)/scripts/verbump.py

upload-release: $(distdir).tar.bz2
	git log origin/master..master
	@echo -n "Ok to push? [y/N] "; read ans; test x$$ans == xy || exit 1
	git push --tags origin master:master
	scp $(distdir).tar.bz2 master.gnome.org:
	ssh master.gnome.org install-module $(distdir).tar.bz2
