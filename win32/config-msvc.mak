# NMake Makefile portion for enabling features for Windows builds

# Spidermonkey release series (17, 24, 31, 38, 45 etc.)
MOZJS_VERSION = 60

# Please see https://bugzilla.gnome.org/show_bug.cgi?id=775868,
# comments 26, 27 and 28
!if "$(MOZJS_VERSION)" == "31"
MOZ_BUG_WORKAROUND_CFLAG = /DJSGC_USE_EXACT_ROOTING=1
!else
MOZ_BUG_WORKAROUND_CFLAG =
!endif

# These are the base minimum libraries required for building gjs.
BASE_INCLUDES =								\
	/I$(PREFIX)\include\gobject-introspection-1.0\girepository	\
	/I$(PREFIX)\include\glib-2.0					\
	/I$(PREFIX)\lib\glib-2.0\include				\
	/I$(PREFIX)\include\mozjs-$(MOZJS_VERSION)			\
	/I$(PREFIX)\include

GJS_BASE_LIBS = gio-2.0.lib gobject-2.0.lib glib-2.0.lib
LIBGJS_BASE_DEP_LIBS =			\
	girepository-1.0.lib		\
	$(GJS_BASE_LIBS)		\
	ffi.lib				\
	intl.lib			\
	mozjs-$(MOZJS_VERSION).lib

# For Cairo support
CAIRO_LIBS = cairo-gobject.lib cairo.lib

# For GTK+ support
GTK_INCLUDES =					\
	/I$(PREFIX)\include\gtk-3.0		\
	/I$(PREFIX)\include\gdk-pixbuf-2.0	\
	/I$(PREFIX)\include\pango-1.0		\
	/I$(PREFIX)\include\atk-1.0

GTK_LIBS = gtk-3.0.lib gdk-3.0.lib

# Please do not change anything beneath this line unless maintaining the NMake Makefiles
# Bare minimum features and sources built into GJS on Windows

# We build the resource module sources directly into the gjs DLL, not as a separate .lib,
# so that we don't have to worry about the Visual Studio linker dropping items during
# optimization
GJS_DEFINES =
GJS_INCLUDED_MODULES =				\
	$(CFG)\$(PLAT)\module-console.lib	\
	$(CFG)\$(PLAT)\module-system.lib

GJS_BASE_CFLAGS =			\
	/I..				\
	/FImsvc_recommended_pragmas.h	\
	/FIjs\RequiredDefines.h		\
	/Dssize_t=gssize		\
	/DG_LOG_DOMAIN=\"Gjs\"		\
	/wd4530				\
	/wd4099				\
	/wd4251				\
	/wd4800				\
	/Zc:externConstexpr

LIBGJS_DEP_INCLUDES = $(BASE_INCLUDES)
LIBGJS_DEP_LIBS = $(LIBGJS_BASE_DEP_LIBS)

LIBGJS_PRIVATE_SOURCES = $(gjs_private_srcs)
LIBGJS_HEADERS = $(gjs_public_headers:/=\)

# We build libgjs and gjs-console at least
GJS_LIBS = $(CFG)\$(PLAT)\gjs.lib

GJS_UTILS = $(CFG)\$(PLAT)\gjs-console.exe
GJS_TESTS =

# Enable Cairo
!if "$(NO_CAIRO)" != "1"
GJS_DEFINES = $(GJS_DEFINES) /DENABLE_CAIRO
GJS_INCLUDED_MODULES =		\
	$(GJS_INCLUDED_MODULES)	\
	$(CFG)\$(PLAT)\module-cairo.lib
LIBGJS_DEP_LIBS = $(CAIRO_LIBS) $(LIBGJS_DEP_LIBS)
!endif

INTROSPECTION_INCLUDE_PACKAGES = --include=Gio-2.0 --include=GObject-2.0

# Enable GTK+
!if "$(NO_GTK)" != "1"
GJS_DEFINES = $(GJS_DEFINES) /DENABLE_GTK
LIBGJS_PRIVATE_SOURCES = $(LIBGJS_PRIVATE_SOURCES) $(gjs_gtk_private_srcs)

GJS_INTROSPECTION_CHECK_PACKAGE = gtk+-3.0
INTROSPECTION_INCLUDE_PACKAGES = $(INTROSPECTION_INCLUDE_PACKAGES) --include=Gtk-3.0
LIBGJS_DEP_INCLUDES = $(GTK_INCLUDES) $(LIBGJS_DEP_INCLUDES)
LIBGJS_DEP_LIBS = $(GTK_LIBS) $(LIBGJS_DEP_LIBS)
!else
GJS_INTROSPECTION_CHECK_PACKAGE = gio-2.0
!endif

LIBGJS_SOURCES = $(gjs_srcs) $(LIBGJS_PRIVATE_SOURCES)

# Use libtool-style DLL names, if desired
!if "$(LIBTOOL_DLL_NAME)" == "1"
LIBGJS_DLL_FILENAME = $(CFG)\$(PLAT)\libgjs-0
!else
LIBGJS_DLL_FILENAME = $(CFG)\$(PLAT)\gjs-vs$(VSVER)
!endif

TEST_PROGRAMS =

# Enable Introspection
!if "$(INTROSPECTION)" == "1"
CHECK_PACKAGE = $(GJS_INTROSPECTION_CHECK_PACKAGE)
EXTRA_TARGETS = $(CFG)\$(PLAT)\GjsPrivate-1.0.gir $(CFG)\$(PLAT)\GjsPrivate-1.0.typelib
!else
EXTRA_TARGETS =
!endif

# Put together the CFLAGS
LIBGJS_CFLAGS =				\
	$(GJS_DEFINES)			\
	$(MOZ_BUG_WORKAROUND_CFLAG)	\
	/DGJS_COMPILATION		\
	/DXP_WIN			\
	/DWIN32				\
	$(GJS_BASE_CFLAGS)		\
	$(LIBGJS_DEP_INCLUDES)

GJS_CFLAGS =				\
	$(GJS_DEFINES)			\
	$(MOZ_BUG_WORKAROUND_CFLAG)	\
	$(GJS_BASE_CFLAGS)		\
	$(BASE_INCLUDES)
