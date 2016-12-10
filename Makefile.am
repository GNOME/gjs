# http://people.gnome.org/~walters/docs/build-api.txt
.buildapi-allow-builddir:

-include $(INTROSPECTION_MAKEFILE)

bin_PROGRAMS =
lib_LTLIBRARIES =
noinst_HEADERS =
noinst_LTLIBRARIES =
noinst_DATA =
dist_gjsjs_DATA =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST =
check_PROGRAMS =
check_LTLIBRARIES =
TESTS = $(check_PROGRAMS)
INTROSPECTION_GIRS =
## ACLOCAL_AMFLAGS can be removed for Automake 1.13
ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = $(WARN_CXXFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_CPPFLAGS = -DG_LOG_DOMAIN=\"Gjs\" $(CODE_COVERAGE_CPPFLAGS)
AM_LDFLAGS = $(WARN_LDFLAGS) $(CODE_COVERAGE_LDFLAGS)
MAINTAINERCLEANFILES =					\
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	tap-driver.sh					\
	$(NULL)
GITIGNOREFILES = INSTALL m4

gjsjsdir = @gjsjsdir@
gjsoverridedir = $(gjsjsdir)/overrides

gjs_public_includedir = $(includedir)/gjs-1.0

########################################################################
nobase_gjs_public_include_HEADERS =	\
	gjs/context.h		\
	gjs/coverage.h			\
	gjs/gjs.h			\
	util/error.h			\
	$(NULL)

########################################################################
pkgconfig_DATA = gjs-1.0.pc

EXTRA_DIST += gjs-1.0.pc.in

########################################################################
gjs_directory_defines = 				\
	-DGJS_TOP_SRCDIR=\"$(top_srcdir)\"		\
	-DGJS_TOP_BUILDDIR=\"$(top_builddir)\"		\
	-DGJS_JS_DIR=\"$(gjsjsdir)\"			\
	-DPKGLIBDIR=\"$(pkglibdir)\"

########################################################################
lib_LTLIBRARIES += libgjs.la

libgjs_la_CPPFLAGS =		\
	$(AM_CPPFLAGS)		\
	$(GJS_CFLAGS)	\
	$(gjs_directory_defines)\
	-I$(top_srcdir)/gi	\
	-DGJS_COMPILATION
libgjs_la_LDFLAGS = 		\
	-export-symbols-regex "^[^_]" -version-info 0:0:0	\
	-no-undefined \
	$(NULL)
libgjs_la_LIBADD = 		\
	$(GJS_LIBS)

if ENABLE_GTK
libgjs_la_CPPFLAGS += $(GJS_GTK_CFLAGS)
libgjs_la_LIBADD += $(GJS_GTK_LIBS)
endif

libgjs_la_SOURCES =		\
	gjs/byteArray.cpp		\
	gjs/context.cpp		\
	gjs/context-private.h		\
	gjs/importer.cpp		\
	gjs/gi.h		\
	gjs/gi.cpp		\
	gjs/coverage-internal.h	\
	gjs/coverage.cpp \
	gjs/jsapi-constructor-proxy.cpp	\
	gjs/jsapi-constructor-proxy.h	\
	gjs/jsapi-private.cpp	\
	gjs/jsapi-private.h		\
	gjs/jsapi-util.cpp	\
	gjs/jsapi-dynamic-class.cpp \
	gjs/jsapi-util-args.h		\
	gjs/jsapi-util-error.cpp	\
	gjs/jsapi-util-string.cpp	\
	gjs/mem.cpp		\
	gjs/native.cpp		\
	gjs/runtime.cpp		\
	gjs/stack.cpp		\
	gjs/type-module.cpp	\
	modules/modules.cpp	\
	modules/modules.h	\
	util/error.cpp		\
	util/hash-x32.cpp		\
	util/hash-x32.h			\
	util/glib.cpp		\
	util/glib.h			\
	util/log.cpp		\
	util/log.h			\
	util/misc.cpp			\
	util/misc.h			\
	$(NULL)

# For historical reasons, some files live in gi/
libgjs_la_SOURCES += \
	gi/gjs_gi_trace.h \
	gi/arg.cpp	\
	gi/boxed.cpp	\
	gi/closure.cpp	\
	gi/enumeration.cpp	\
	gi/function.cpp	\
	gi/keep-alive.cpp	\
	gi/ns.cpp	\
	gi/object.cpp	\
	gi/foreign.cpp	\
	gi/fundamental.cpp	\
	gi/param.cpp	\
	gi/proxyutils.cpp	\
	gi/proxyutils.h		\
	gi/repo.cpp	\
	gi/union.cpp	\
	gi/value.cpp	\
	gi/interface.cpp	\
	gi/gtype.cpp	\
	gi/gerror.cpp

# Also, these files used to be a separate library
libgjs_private_source_files = \
	libgjs-private/gjs-gdbus-wrapper.cpp	\
	libgjs-private/gjs-gdbus-wrapper.h	\
	libgjs-private/gjs-util.cpp		\
	libgjs-private/gjs-util.h		\
	libgjs-private/gjs-gtk-util.h

if ENABLE_GTK
libgjs_private_source_files += 			\
	libgjs-private/gjs-gtk-util.c		\
	$(NULL)
endif

libgjs_la_SOURCES += $(libgjs_private_source_files)

# These used to be public headers for external modules
libgjs_la_SOURCES +=		\
	gjs/byteArray.h		\
	gjs/importer.h		\
	gjs/jsapi-util.h	\
	gjs/jsapi-wrapper.h	\
	gjs/runtime.h		\
	gjs/type-module.h	\
	gjs/mem.h		\
	gjs/native.h		\
	gi/ns.h	        	\
	gi/object.h		\
	gi/foreign.h		\
	gi/fundamental.h	\
	gi/param.h		\
	gi/repo.h		\
	gi/union.h		\
	gi/value.h		\
	gi/arg.h		\
	gi/boxed.h		\
	gi/closure.h		\
	gi/enumeration.h	\
	gi/function.h		\
	gi/keep-alive.h		\
	gi/interface.h		\
	gi/gtype.h		\
	gi/gerror.h		\
	$(NULL)

GjsPrivate-1.0.gir: libgjs.la
GjsPrivate_1_0_gir_LIBS = libgjs.la
GjsPrivate_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
GjsPrivate_1_0_gir_FILES = $(libgjs_private_source_files)
GjsPrivate_1_0_gir_SCANNERFLAGS =	\
	--identifier-prefix=Gjs		\
	--symbol-prefix=gjs_		\
	--warn-all			\
	$(WARN_SCANNERFLAGS)		\
	$(NULL)

if ENABLE_GTK
GjsPrivate_1_0_gir_INCLUDES += Gtk-3.0
endif

INTROSPECTION_GIRS += GjsPrivate-1.0.gir

if ENABLE_DTRACE
gjs_gi_probes.h: gi/gjs_gi_probes.d
	$(DTRACE) -C -h -s $< -o $@
gjs_gi_probes.o: gi/gjs_gi_probes.d
	$(DTRACE) -G -s $< -o $@
BUILT_SOURCES += gjs_gi_probes.h gjs_gi_probes.o
libgjs_la_LIBADD += gjs_gi_probes.o
endif
EXTRA_DIST += gi/gjs_gi_probes.d

tapset_in_files = gjs/gjs.stp.in
EXTRA_DIST += $(tapset_in_files)
if ENABLE_SYSTEMTAP
gjs/gjs.stp: gjs/gjs.stp.in Makefile
	$(AM_V_GEN)$(MKDIR_P) $(@D) && \
	$(SED) -e s,@EXPANDED_LIBDIR@,$(libdir), < $< > $@.tmp && mv $@.tmp $@
tapsetdir   = $(datadir)/systemtap/tapset
tapset_DATA = $(tapset_in_files:.stp.in=.stp)
endif

include Makefile-modules.am
include Makefile-examples.am

typelibdir = $(pkglibdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(INTROSPECTION_GIRS) $(typelib_DATA)

########################################################################
bin_PROGRAMS += gjs-console

gjs_console_CPPFLAGS = 		\
	$(AM_CPPFLAGS)		\
	$(GJS_CONSOLE_CFLAGS)	\
	$(NULL)
gjs_console_LDADD =		\
	$(GJS_CONSOLE_LIBS)	\
	libgjs.la
gjs_console_LDFLAGS = -rdynamic
gjs_console_SOURCES = gjs/console.cpp

install-exec-hook:
	(cd $(DESTDIR)$(bindir) && $(LN_S) -f gjs-console$(EXEEXT) gjs$(EXEEXT))

include Makefile-test.am
include Makefile-insttest.am

EXTRA_DIST +=				\
	autogen.sh			\
	COPYING.LGPL			\
	doc/ByteArray.md		\
	doc/cairo.md			\
	doc/Class_Framework.md		\
	doc/SpiderMonkey_Memory.md	\
	doc/Style_Guide.md		\
	$(NULL)

# Colin's handy Makefile bits for:
# 1) stuffing tarballs with pre-generated scripts from your workstation
# 2) bumping configure.ac version post-release
# 3) tagging correctly in git
# 4) uploading to gnome.org
# To use:
#  $ make check
#  $ make dist
#  $ make prepare-minor-release

# Customize to taste
TAG_PREFIX=GJS_
COMPRESSION=.bz2


PACKAGE=@PACKAGE@
VERSION=@VERSION@
DISTNAME=$(PACKAGE)-$(VERSION).tar$(COMPRESSION)
TAG_VERSION := $(shell echo $(VERSION) | $(SED) s/\\\./_/g)

prepare-release-tag: Makefile
	git tag -m "Tag $(TAG_VERSION)" -a $(TAG_PREFIX)$(TAG_VERSION)

prepare-minor-release: $(DISTNAME) prepare-release-tag Makefile
	env top_srcdir=$(top_srcdir) python $(top_srcdir)/verbump.py

upload-release: $(DISTNAME) Makefile
	git log origin/master..master
	@echo -n "Ok to push? [y/N] "; read ans; test x$$ans == xy || exit 1
	git push --tags origin master:master
	scp $(DISTNAME) master.gnome.org:
	ssh master.gnome.org install-module $(DISTNAME)

-include $(top_srcdir)/git.mk
