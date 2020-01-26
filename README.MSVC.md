Instructions for building GJS on Visual Studio
==============================================
Building the GJS on Windows is now supported using Visual Studio
versions 2017 15.6.x or later in both 32-bit and 64-bit (x64) flavors,
via Meson.  Due to C++-14 usage, Visual Studio 2017 15.6.x or later is
required, as the compiler flag /Zc:externConstexpr is needed.

You will need the following items to build GJS using Visual Studio:
-SpiderMonkey 68 (mozjs-68).  Please see the below section carefully 
 on this...
-GObject-Introspection (G-I) 1.61.2 or later
-GLib 2.58.x or later, (which includes GIO, GObject, and the associated tools)
-Cairo including Cairo-GObject support (Optional)
-GTK+-3.20.x or later (Optional)
-and anything that the above items depends on.

Note that SpiderMonkey must be built with Visual Studio, and the rest
should preferably be built with Visual Studio as well.  The Visual 
Studio version used should preferably be the one that is used here
to build GJS.

Be aware that it is often hard to find a suitable source release for
SpiderMonkey nowadays, so it may be helpful to look in

ftp://ftp.gnome.org/pub/gnome/teams/releng/tarballs-needing-help/mozjs/

for the suitable release series of SpiderMonkey that corresponds to 
the GJS version that is being built, as GJS depends on ESR (Extended 
Service Release, a.k.a Long-term support) releases of SpiderMonkey.

Please do note that the build must be done carefully, in addition to the
official instructions that are posted on the Mozilla website:

https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/Build_Documentation

For the configuration step, you will need to run the following:

(64-bit/x64 builds)
JS_STANDALONE=1 $(mozjs_srcroot)/js/src/configure --enable-nspr-build --host=x86_64-pc-mingw32 --target=x86_64-pc-mingw32 --prefix=--prefix=<some_prefix> --disable-jemalloc

(32-bit builds)
JS_STANDALONE=1 $(mozjs_srcroot)/js/src/configure --enable-nspr-build --prefix=<some_prefix> --disable-jemalloc

Notice that "JS_STANDALONE=1" and "--disable-jemalloc" are absolutely required,
otherwise GJS will not build/run correctly.  If your GJS build crashes upon
launch, use Depedency Walker to ensure that mozjs-68.dll does not depend on
mozglue.dll!  If it does, or if GJS fails to link with missing arena_malloc() and
friends symbols, you have built SpiderMoney incorrectly and will need to rebuild
SpiderMonkey (with the build options as noted above) and retry the build.
Note in particular that a mozglue.dll should *not* be in $(builddir)/dist/bin,
although there will be a mozglue.lib somewhere in the build tree (which, you can
safely delete after building SpiderMonkey).  The --host=... and --target=...
are absolutely required for x64 builds, as per the Mozilla's SpiderMonkey build
instructions.

You may want to pass in --disable-js-shell to not build the JS
shell that comes with SpiderMonkey to save time, and perhaps
use --with-system-nspr (instead of the --enable-nspr-build as
above), --with-system-zlib and --with-system-icu if you know
what you are doing and that their pkg-config files
(or headers/LIB's) can be found directly or using configuration 
options, to further save time.

After the configuration finishes successfully, you may run 'mozmake' and
'mozmake install' as you would for a standard SpiderMonkey build.  If
'mozmake install' does not work for you for some reason, the DLLs you 
need and js.exe (if you did not pass in --disable-js-shell) can be 
found in $(buildroot)/dist/bin (you need *all* the DLLs, make sure 
that there is no mozglue.dll, otherwise you will need to redo your 
build as noted above), and the required headers are found in
$(buildroot)/dist/include.  Note that for PDB files and .lib files, 
you will need to search for them in $(buildroot),
where the PDB file names match the filenames for the DLLs/EXEs in
$(buildroot)/dist/bin, and you will need to look for the following .lib files:
-mozjs-68.lib
-js_static.lib (optional)
-nspr4.lib (optional, recommended for future use, if --enable-nspr-build is used)
-plc4.lib (optional, recommended for future use, if --enable-nspr-build is used)
-plds4.lib (optional, recommended for future use, if --enable-nspr-build is used)

You may want to put the .lib's and DLLs/EXEs into $(PREFIX)\lib and 
$(PREFIX)\bin respectively, and put the headers into
$(PREFIX)\include\mozjs-68 for convenience.

You will need to place the generated mozjs-68.pc pkg-config file into
$(PREFIX)\lib\pkgconfig and ensure that pkg-config can find it by
setting PKG_CONFIG_PATH.  Ensure that the 'includedir' and 'libdir'
in there is correct, and remove the 'nspr' entry from the
'Requires.private:' line and change
'-include ${includedir}/mozjs-68/js/RequiredDefines.h' to
'-FI${includedir}/mozjs-68/js/RequiredDefines.h', so that the
mozjs-68.pc can be used correctly in Visual Studio builds.  You
will also need to ensure that the existing GObject-Introspection
installation (if used) is on the same drive where the GJS sources
are (and therefore where the GJS build is being carried out).

======================
To carry out the build
======================
You need to install Python 3.5.x or later, as well as the
pkg-config tool, Meson (via pip) and Ninja (unless using
--backend=vs[2017|2019]).  Perform a build by doing the
following, in an appropriate Visual Studio command prompt
in an empty build directory:

meson <path_to_gjs_sources> --buildtype=... --prefix=<some_prefix> -Dskip_dbus_tests=true <--backend=vs[2017|2019]>

(Note that -Dskip_dbus_tests=true is required for MSVC builds; please
see the Meson documentation for the values accepted by buildtype)

You may want to view the build options after the configuration succeeds
by using 'meson configure'

When the configuration succeeds, run:
ninja
(to build the sources, or open the generated .sln file using
Visual Studio 2017 or 2019 if --backend=vs[2017|2019] is used)

You may choose to install the build results using 'ninja install'
or running the 'install' project when the build succeeds.
