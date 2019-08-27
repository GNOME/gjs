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
INTROSPECTION_GIRS =
## ACLOCAL_AMFLAGS can be removed for Automake 1.13
ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = $(WARN_CFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_CXXFLAGS = $(WARN_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)
AM_CPPFLAGS = -DG_LOG_DOMAIN=\"Gjs\" $(CODE_COVERAGE_CPPFLAGS)
AM_LDFLAGS = $(WARN_LDFLAGS) $(CODE_COVERAGE_LIBS)
MAINTAINERCLEANFILES =					\
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	tap-driver.sh					\
	$(NULL)
GITIGNOREFILES = INSTALL m4 tools/__pycache__

gjsjsdir = @gjsjsdir@
gjsoverridedir = $(gjsjsdir)/overrides

gjs_public_includedir = $(includedir)/gjs-1.0

########################################################################
include gjs-srcs.mk
include gjs-modules-srcs.mk
########################################################################
nobase_gjs_public_include_HEADERS = $(gjs_public_headers)

########################################################################
pkgconfig_DATA = gjs-1.0.pc

EXTRA_DIST += gjs-1.0.pc.in

valgrinddir = $(datadir)/gjs-1.0/valgrind
dist_valgrind_DATA = installed-tests/extra/gjs.supp

lsandir = $(datadir)/gjs-1.0/lsan
dist_lsan_DATA = installed-tests/extra/lsan.supp

########################################################################
gjs_directory_defines = 				\
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
libgjs_la_LDFLAGS = 			\
	$(AM_LDFLAGS)			\
	-export-symbols-regex "^[^_]"	\
	-version-info 0:0:0		\
	$(NO_UNDEFINED_FLAG)		\
	$(NULL)
libgjs_la_LIBADD = 		\
	$(GJS_LIBS)		\
	$(READLINE_LIBS)	\
	$(NULL)

# Please see gjs-srcs.mk for brief expanations
# of the layout of the sources due to historical
# reasons
libgjs_la_SOURCES = $(gjs_srcs)

if ENABLE_PROFILER
libgjs_la_CPPFLAGS += $(SYSPROF_CAPTURE_CFLAGS)
libgjs_la_LIBADD += $(LIB_TIMER_TIME) $(SYSPROF_CAPTURE_LIBS)
endif

# Also, these files used to be a separate library
libgjs_private_source_files = $(gjs_private_srcs)

libgjs_la_SOURCES += $(libgjs_private_source_files)

# The built-in modules also used to be compiled separately

modules_resource_files := $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/modules/modules.gresource.xml)
modules-resources.h: $(srcdir)/modules/modules.gresource.xml $(modules_resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --sourcedir=$(builddir) --generate --c-name modules_resources $<
modules-resources.c: $(srcdir)/modules/modules.gresource.xml $(modules_resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --sourcedir=$(builddir) --generate --c-name modules_resources $<

EXTRA_DIST += $(modules_resource_files) $(srcdir)/modules/modules.gresource.xml

modules_source_files =		\
	$(module_system_srcs)	\
	$(module_console_srcs)	\
	$(NULL)

if ENABLE_CAIRO
modules_source_files += $(module_cairo_srcs)
libgjs_la_CPPFLAGS += $(GJS_CAIRO_CFLAGS) $(GJS_CAIRO_XLIB_CFLAGS)
libgjs_la_LIBADD += $(GJS_CAIRO_LIBS) $(GJS_CAIRO_XLIB_LIBS)
endif

CLEANFILES += $(module_resource_srcs)

libgjs_la_SOURCES += $(modules_source_files)
nodist_libgjs_la_SOURCES = $(module_resource_srcs)

GjsPrivate-1.0.gir: libgjs.la
GjsPrivate_1_0_gir_LIBS = libgjs.la
GjsPrivate_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
GjsPrivate_1_0_gir_CFLAGS = -I$(top_srcdir)
GjsPrivate_1_0_gir_FILES = $(libgjs_private_source_files)
GjsPrivate_1_0_gir_SCANNERFLAGS =	\
	--identifier-prefix=Gjs		\
	--symbol-prefix=gjs_		\
	--warn-all			\
	$(WARN_SCANNERFLAGS)		\
	$(NULL)

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
gjs_console_LDFLAGS = $(AM_LDFLAGS) -rdynamic
gjs_console_SOURCES = $(gjs_console_srcs)

install-exec-hook:
	(cd $(DESTDIR)$(bindir) && $(LN_S) -f gjs-console$(EXEEXT) gjs$(EXEEXT))

include Makefile-test.am
include Makefile-insttest.am

EXTRA_DIST +=					\
	autogen.sh				\
	build/choose-tests-locale.sh		\
	build/compile-gschemas.sh		\
	COPYING.LGPL				\
	doc/ByteArray.md			\
	doc/cairo.md				\
	doc/Hacking.md				\
	doc/SpiderMonkey_Memory.md		\
	doc/Style_Guide.md			\
	installed-tests/js/meson.build		\
	installed-tests/meson.build		\
	libgjs.map				\
	libgjs.symbols				\
	meson.build				\
	meson_options.txt			\
	subprojects/glib.wrap			\
	subprojects/gobject-introspection.wrap	\
	subprojects/libffi.wrap			\
	subprojects/proxy-libintl.wrap		\
	subprojects/zlib.wrap			\
	test/meson.build			\
	win32/build-rules-msvc.mak		\
	win32/config-msvc.mak			\
	win32/config.h.win32			\
	win32/create-lists-msvc.mak		\
	win32/create-lists.bat			\
	win32/detectenv-msvc.mak		\
	win32/generate-msvc.mak			\
	win32/gjs-introspection-msvc.mak	\
	win32/info-msvc.mak			\
	win32/install.mak			\
	win32/introspection-msvc.mak		\
	win32/Makefile.vc			\
	win32/README.txt			\
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

DISTCLEANFILES=gjs-*.syscap

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


CPPCHECK=cppcheck
### cppcheck static code analysis
#
cppcheck:
	$(CPPCHECK) --inline-suppr \
		--enable=warning,performance,portability,information,missingInclude \
		--force -q $(top_srcdir) -I $(top_builddir)

-include $(top_srcdir)/git.mk
