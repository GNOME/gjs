Instructions for building GJS on Visual Studio
==============================================
Building the GJS on Windows is now supported using Visual Studio
versions 2013 or later in both 32-bit and 64-bit (x64) flavors,
via NMake Makefiles.  Due to C++-11 usage, Visual Studio 2012 or
earlier is not supported.

You will need the following items to build GJS using Visual Studio:
-SpiderMonkey 38 (mozjs-38)
-GObject-Introspection (G-I) 1.41.4 or later
-GLib 2.50.x or later, (which includes GIO, GObject, and the associated tools)
-Cairo including Cairo-GObject support, unless NO_CAIRO=1 is specified.
-GTK+-3.20.x or later, unless NO_GTK=1 is specified.
-and anything that the above items depends on.

Note that SpiderMonkey must be built with Visual Studio, and the rest
should preferably be built with Visual Studio as well.  The Visual Studio
version used should preferably be the one that is used here to build GJS.

If you built SpiderMonkey 38 using the normal build instructions as described
on Mozilla's website, you may notice that the output library, DLLs and include
directory might not be what one may expect, which is likely due to bugs in its build
scripts.  If this is the case, rename mozjs-.lib to mozjs-38.lib, and the
include directory from mozjs- to mozjs-38 (but please do *not* rename mozjs-.dll
and mozjs-.pdb, as they will be searched for when gjs-console.exe/gjs.exe runs,
along with any program that uses the GJS DLL).  Otherwise, do (or redo) the
SpiderMonkey build process (including running configure) after applying the patch
from https://git.gnome.org/browse/jhbuild/tree/patches/mozjs38-release-number.patch

The following are instructions for performing such a build, as there is a
number of build configurations supported for the build.  Note that the default
build (where no options (see below) are specified, the GJS library is built with
Cairo and GTK+ support.  A 'clean' target is provided-it is recommended that
one cleans the build and redo the build if any configuration option changed.  An
'install' target is also provided to copy the built items in their appropriate
locations under $(PREFIX), which is described below.

Invoke the build by issuing the command:
nmake /f Makefile.vc CFG=[release|debug] [PREFIX=...] <option1=1 option2=1 ...>
where:

CFG: Required.  Choose from a release or debug build.  Note that
     all builds generate a .pdb file for each .dll and .exe built--this refers
     to the C/C++ runtime that the build uses.

PREFIX: Optional.  Base directory of where the third-party headers, libraries
        and needed tools can be found, i.e. headers in $(PREFIX)\include,
        libraries in $(PREFIX)\lib and tools in $(PREFIX)\bin.  If not
        specified, $(PREFIX) is set as $(srcroot)\..\vs$(X)\$(platform), where
        $(platform) is win32 for 32-bit builds or x64 for 64-bit builds, and
        $(X) is the short version of the Visual Studio used, as follows:
        2013: 12
        2015: 14

Explanation of options, set by <option>=1:
------------------------------------------
NO_CAIRO: Disables Cairo support in the GJS DLL.

NO_GTK: Disables GTK+ support in the GJS DLL

INTROSPECTION: Enable build of introspection files, for making
               bindings for other programming languages available, such as
               Python.  This requires the GObject-Introspection
               libraries and tools, along with the Python interpreter that was
               used during the build of GObject-Introspection.  This will
               require the introspection files for GTK+, unless NO_GTK=1 is
               specified, where the introspection files for GIO will be
               required.

PYTHON: Full path to the Python interpreter to be used, if it is not in %PATH%.

LIBTOOL_DLL_NAME: Enable libtool-style DLL names.  Note this does not make this
                  GJS build usable by other compilers, due to C++ usage.
