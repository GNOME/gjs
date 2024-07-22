Instructions for building GJS on Visual Studio or clang-cl
==========================================================
Building the GJS on Windows is now supported using Visual Studio
versions 2019 16.5.x or later with or without clang-cl in both 32-bit
and 64-bit (x64) flavors, via Meson.  It should be noted that a
recent-enough Windows SDK from Microsoft is still required if using
clang-cl, as we will still use items from the Windows SDK.

Recent official binary installers of CLang (which contains clang-cl)
from the LLVM website are known to work to build SpiderMonkey 128 and
GJS.

You will need the following items to build GJS using Visual Studio
or clang-cl (they can be built with Visual Studio 2015 or later,
unless otherwise noted):
- SpiderMonkey 128.x (mozjs-128). This must be built with clang-cl as
  the Visual Studio  compiler is no longer supported for building this.
  Please see the below section carefully on this...
- GObject-Introspection (G-I) 1.66.x or later
- GLib 2.66.x or later, (which includes GIO, GObject, and the
  associated tools)
- Cairo including Cairo-GObject support (Optional)
- GTK+-4.x or later (Optional)
- and anything that the above items depend on.

Note again that SpiderMonkey must be built using Visual Studio with
clang-cl, and the rest should preferably be built with Visual Studio
or clang-cl as well.  The Visual Studio version used for building the
other dependencies should preferably be the same across the board, or,
if using Visual Studio 2015 or later, Visual Studio 2015 through 2022.

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

You may also be able to obtain the SpiderMonkey 128.x sources via the
FireFox (ESR) or Thunderbird 128.x sources, in $(srcroot)/js.

Please do note that the build must be done carefully, in addition to the
official instructions that are posted on the Mozilla website:

https://firefox-source-docs.mozilla.org/js/build.html

You will need to create a .mozconfig file that will describe your build
options for the build in the root directory of the Firefox/ThunderBird 128.x
sources.  A sample content of the .mozconfig file can be added as follows:

```
ac_add_options --enable-application=js
mk_add_options MOZ_MAKE_FLAGS=-j12
ac_add_options --target=x86_64-pc-mingw32
ac_add_options --host=x86_64-pc-mingw32
ac_add_options --disable-tests
ac_add_options --enable-optimize
ac_add_options --disable-debug
ac_add_options --disable-jemalloc
ac_add_options --prefix=c:/software.b/mozjs128.bin
```

An explanation of the lines above:
*  `ac_add_options --enable-application=js`: This line is absolutely required, to build SpiderMonkey standalone
*  `mk_add_options MOZ_MAKE_FLAGS=-j12`:  MOZ_MAKE_FLAGS=-jX means X number of parallel processes for the build
*  `ac_add_options --target=x86_64-pc-mingw32`: Target architecture, replace `x86_64` with `aarch64` for ARM64 builds, and with `i686` for 32-bit x86 builds.
*  `ac_add_options --host=x86_64-pc-mingw32`: Use this as-is, unless building on a 32-bit compiler (replace `x86_64` with `i686`; not recommended)
*  `ac_add_options --disable-tests`: Save some build time
*  `ac_add_options --enable-optimize`: Use for release builds of SpiderMonkey.  Use `--disable-optimize` instead if building with `--enable-debug`
*  `ac_add_options --enable-debug`: Include debugging functions, for debug builds.  Use `--disable-debug` instead if building with `--enable-optimize`
*  `ac_add_options --disable-jemalloc`: This is absolutely needed, otherwise GJS will not build and run correctly
*  `ac_add_options --prefix=c:/software.b/mozjs128.bin`: Some installation path, change as needed

If your GJS build crashes upon launch, use Dependency Walker to ensure that
mozjs-128.dll does not depend on mozglue.dll!  If it does, or if GJS fails to
link with missing arena_malloc() and friends symbols, you have built SpiderMoney
incorrectly and will need to rebuild SpiderMonkey (with the build options as
noted above) and retry the build.

Please also check that `--enable-optimize` is *not* used with `--enable-debug`.
You should explicitly enable one and disable the other, as `--enable-debug`
will make the resulting build depend on the debug CRT, and mixing between
the release and debug CRT in the same DLL is often a sign of trouble when using
with GJS, meaning that you will need to rebuild SpiderMonkey with the appropriate
options set in your `.mozconfig` file.  Please note that for SpiderMonkey builds, 
PDB files are generated even if `--disable-debug` is used.

You will need to check that `js-config.h` has the correct entries that correspond
to your SpiderMonkey build, especially the following items:

*  `JS_64BIT`, `JS_PUNBOX64`: Should be defined for 64-bit builds, not 32-bit builds
*  `JS_NUNBOX32`: Should be defined for 32-bit builds, not 64-bit builds
*  `JS_DEBUG`, `JS_GC_ZEAL`: Should only be defined if `--enable-debug` is used

Note in particular that a mozglue.dll should *not* be in $(builddir)/dist/bin,
although there will be a mozglue.lib somewhere in the build tree (which, you can
safely delete after building SpiderMonkey).  The --host=... and --target=...
are absolutely required for all builds, as per the Mozilla's SpiderMonkey build
instructions, as Rust is being involved here.

Run `./mach build` to carry out the build, and then `./mach build install` to copy
the completed build to the directory specified by `ac_add_options --prefix=xxx`.

If `./mach build install` does not work for you for some reason, the DLLs you 
need and js.exe can be found in $(buildroot)/dist/bin (you need *all* the DLLs,
make sure that there is no mozglue.dll, otherwise you will need to redo your 
build as noted above), and the required headers are found in
$(buildroot)/dist/include.  Note that for PDB files and .lib files, 
you will need to search for them in $(buildroot),
where the PDB file names match the filenames for the DLLs/EXEs in
$(buildroot)/dist/bin, and you will need to look for the following .lib files:
-mozjs-128.lib
-js_static.lib (optional)

You may want to put the .lib's and DLLs/EXEs into $(PREFIX)\lib and 
$(PREFIX)\bin respectively, and put the headers into
$(PREFIX)\include\mozjs-128 for convenience.

You will need to place the generated mozjs-128.pc pkg-config file into
$(PREFIX)\lib\pkgconfig and ensure that pkg-config can find it by
setting PKG_CONFIG_PATH.  Ensure that the 'includedir' and 'libdir'
in there is correct so that the mozjs-128.pc can be used correctly in
Visual Studio/clang-cl builds, and replace the `-isystem` with `-I` if
building GJS with Visual Studio.  You will also need to ensure that the
existing GObject-Introspection installation (if used) is on the same
drive where the GJS sources are (and therefore where the GJS build
is being carried out).

To carry out the build
======================
If using clang-cl, you will need to set *both* the environment variables CC
and CXX to: 'clang-cl [--target=<target_triplet>]' (without the quotes); please
see https://clang.llvm.org/docs/CrossCompilation.html on how the target triplet
can be defined, which is used if using the cross-compilation capabilities of CLang.
In this case, you need to ensure that 'clang-cl.exe' and 'lld-link.exe' (i.e. your
LLVM bindir) are present in your PATH.

You need to install Python 3.6.x or later, as well as the
pkg-config tool, Meson (via pip) and Ninja.  Perform a build by doing the
following, in an appropriate Visual Studio command prompt
in an empty build directory:

```
meson <path_to_gjs_sources> --buildtype=... --prefix=<some_prefix> -Dskip_dbus_tests=true -Dprofiler=disabled
```

(Note that -Dskip_dbus_tests=true is required for MSVC/clang-cl builds; please
see the Meson documentation for the values accepted by buildtype)

You may want to view the build options after the configuration succeeds
by using 'meson configure'.  You may need to set the envvar:
`SETUPTOOLS_USE_DISTUTILS=stdlib` for the introspection step to proceed
successfully.  A fix for this is being investigated.

When the configuration succeeds, run:
ninja

You may choose to install the build results using 'ninja install'
or running the 'install' project when the build succeeds.
