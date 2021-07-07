Instructions for building GJS on Visual Studio or clang-cl
==========================================================
Building the GJS on Windows is now supported using Visual Studio
versions 2019 or later with or without clang-cl in both 32-bit and
64-bit (x64) flavors, via Meson.  It should be noted that a
recent-enough Windows SDK from Microsoft is still required if using
clang-cl, as we will still use items from the Windows SDK.

Recent official binary installers of CLang (which contains clang-cl)
from the LLVM website are known to work to build SpiderMonkey 78 and
GJS.

You will need the following items to build GJS using Visual Studio
or clang-cl (they can be built with Visual Studio 2015 or later,
unless otherwise noted):
-SpiderMonkey 78.x (mozjs-78). This must be built with clang-cl as
 the Visual Studio  compiler is no longer supported for building this.
 Please see the below section carefully on this...
-GObject-Introspection (G-I) 1.61.2 or later
-GLib 2.58.x or later, (which includes GIO, GObject, and the
 associated tools)
-Cairo including Cairo-GObject support (Optional)
-GTK+-3.20.x or later (Optional)
-and anything that the above items depend on.

Note again that SpiderMonkey must be built using Visual Studio with
clang-cl, and the rest should preferably be built with Visual Studio
or clang-cl as well.  The Visual Studio version used for building the
other dependencies should preferably be the same across the board, or,
if using Visual Studio 2015 or later, Visual Studio 2015 through 2019.

Please also be aware that the Rust MSVC toolchains that correspond to
the platform you are building for must also be present to build
SpiderMonkey.  Please refer to the Rust website on how to install the
Rust compilers and toolchains for MSVC.  This applies to clang-cl
builds as well.

Be aware that it is often hard to find a suitable source release for
SpiderMonkey nowadays, so it may be helpful to look in

ftp://ftp.gnome.org/pub/gnome/teams/releng/tarballs-needing-help/mozjs/

for the suitable release series of SpiderMonkey that corresponds to 
the GJS version that is being built, as GJS depends on ESR (Extended 
Service Release, a.k.a Long-term support) releases of SpiderMonkey.

You may also be able to obtain the SpiderMonkey 78.x sources via the
FireFox (ESR) or Thunderbird 78.x sources, in $(srcroot)/js.

Please do note that the build must be done carefully, in addition to the
official instructions that are posted on the Mozilla website:

https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/Build_Documentation

For the configuration step, you will need to run the following:

(64-bit/x64 builds)
JS_STANDALONE=1 $(mozjs_srcroot)/js/src/configure --enable-nspr-build --host=x86_64-pc-mingw32 --target=x86_64-pc-mingw32 --prefix=<some_prefix> --disable-jemalloc --with-libclang-path=<full_path_to_directory_containing_libclang_dll> --with-clang-path=<full_path_to_directory_containing_clang_exe>

(32-bit builds)
JS_STANDALONE=1 $(mozjs_srcroot)/js/src/configure --enable-nspr-build --host=i686-pc-mingw32 --target=i686-pc-mingw32 --prefix=<some_prefix> --disable-jemalloc --with-libclang-path=<full_path_to_directory_containing_libclang_dll> --with-clang-path=<full_path_to_directory_containing_clang_exe>

Notice that "JS_STANDALONE=1" and "--disable-jemalloc" are absolutely required,
otherwise GJS will not build/run correctly.  If your GJS build crashes upon
launch, use Dependency Walker to ensure that mozjs-78.dll does not depend on
mozglue.dll!  If it does, or if GJS fails to link with missing arena_malloc() and
friends symbols, you have built SpiderMoney incorrectly and will need to rebuild
SpiderMonkey (with the build options as noted above) and retry the build.
Note in particular that a mozglue.dll should *not* be in $(builddir)/dist/bin,
although there will be a mozglue.lib somewhere in the build tree (which, you can
safely delete after building SpiderMonkey).  The --host=... and --target=...
are absolutely required for all builds, as per the Mozilla's SpiderMonkey build
instructions, as Rust is being involved here.

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
-mozjs-78.lib
-js_static.lib (optional)
-nspr4.lib (optional, recommended for future use, if --enable-nspr-build is used)
-plc4.lib (optional, recommended for future use, if --enable-nspr-build is used)
-plds4.lib (optional, recommended for future use, if --enable-nspr-build is used)

You may want to put the .lib's and DLLs/EXEs into $(PREFIX)\lib and 
$(PREFIX)\bin respectively, and put the headers into
$(PREFIX)\include\mozjs-78 for convenience.

You will need to place the generated mozjs-78.pc pkg-config file into
$(PREFIX)\lib\pkgconfig and ensure that pkg-config can find it by
setting PKG_CONFIG_PATH.  Ensure that the 'includedir' and 'libdir'
in there is correct, and remove the 'nspr' entry from the
'Requires.private:' line and change
'-include ${includedir}/mozjs-78/js/RequiredDefines.h' to
'-FI${includedir}/mozjs-78/js/RequiredDefines.h', so that the
mozjs-78.pc can be used correctly in Visual Studio/clang-cl builds.  You
will also need to ensure that the existing GObject-Introspection
installation (if used) is on the same drive where the GJS sources
are (and therefore where the GJS build is being carried out).

Since Mozilla insisted that clang-cl is to be used to build SpiderMonkey,
note that some SpideMonkey headers might need be updated as follows, if intending
to build without clang-cl, since there are some GCC-ish assumptions here:

-Update $(includedir)/mozjs-78/js/AllocPolicy.h (after the build):

Get rid of the 'JS_FRIEND_API' macro from the class
'TempAllocPolicy : public AllocPolicyBase' (ca. lines 112 and 178),
for the member method definitions of onOutOfMemory() and reportAllocOverflow()

-Update $(includedir)/mozjs-78/js/BigInt.h (after the build):

Remove the 'JS_PUBLIC_API' macro from the definition of
'template <typename NumericT>
extern BigInt* NumberToBigInt(JSContext* cx, NumericT val)' (ca lines 72-73), as
it should not be there.

======================
To carry out the build
======================
If using clang-cl, you will need to set *both* the environment variables CC
and CXX to: 'clang-cl [--target=<target_triplet>]' (without the quotes); please
see https://clang.llvm.org/docs/CrossCompilation.html on how the target triplet
can be defined, which is used if using the cross-compilation capabilities of CLang.
In this case, you need to ensure that 'clang-cl.exe' and 'lld-link.exe' (i.e. your
LLVM bindir) are present in your PATH.

You need to install Python 3.5.x or later, as well as the
pkg-config tool, Meson (via pip) and Ninja.  Perform a build by doing the
following, in an appropriate Visual Studio command prompt
in an empty build directory:

meson <path_to_gjs_sources> --buildtype=... --prefix=<some_prefix> -Dskip_dbus_tests=true

(Note that -Dskip_dbus_tests=true is required for MSVC/clang-cl builds; please
see the Meson documentation for the values accepted by buildtype)

You may want to view the build options after the configuration succeeds
by using 'meson configure'

When the configuration succeeds, run:
ninja

You may choose to install the build results using 'ninja install'
or running the 'install' project when the build succeeds.
