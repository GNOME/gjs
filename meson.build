# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>
# SPDX-FileCopyrightText: 2019 Chun-wei Fan <fanchunwei@src.gnome.org>

project('gjs', 'cpp', 'c', version: '1.86.0', license: ['MIT', 'LGPL2+'],
    meson_version: '>= 1.4',
    default_options: ['cpp_std=c++17', 'cpp_rtti=false', 'cpp_eh=none',
        'c_std=c99', 'warning_level=2', 'b_pch=true' ])

# cpp_rtti: SpiderMonkey can be compiled with or without runtime type
# information, and the default is without. We must match that option because we
# need to derive from SpiderMonkey classes.

api_version = '1.0'
api_name = '@0@-@1@'.format(meson.project_name(), api_version)

gnome = import('gnome')
pkg = import('pkgconfig')

top_include = include_directories('.')
prefix = get_option('prefix')
bindir = get_option('bindir')
libdir = get_option('libdir')
datadir = get_option('datadir')
libexecdir = get_option('libexecdir')
gjsjsdir = datadir / api_name
pkglibdir = libdir / meson.project_name()
installed_tests_execdir = libexecdir / 'installed-tests' / meson.project_name()
installed_tests_metadir = datadir / 'installed-tests' / meson.project_name()

### Check for conflicting build options ########################################

if get_option('systemtap') and not get_option('dtrace')
    error('-Ddtrace=true is required for -Dsystemtap=true')
endif

release_build = get_option('buildtype').startswith('release')
if release_build and get_option('verbose_logs')
    error('-Dverbose_logs=true is not allowed with --buildtype=release')
endif

### Check for compiler args ####################################################

cxx = meson.get_compiler('cpp')
cc = meson.get_compiler('c')

if cc.get_id() == 'msvc'
    add_project_arguments(cxx.get_supported_arguments([
        '-utf-8',  # Use UTF-8 mode
        '/Zc:externConstexpr',  # Required for 'extern constexpr' on MSVC
        '/Zc:preprocessor',     # Required to consume the mozjs headers on MSVC

        # Ignore spurious compiler warnings for things that GLib and SpiderMonkey
        # header files commonly do
        '-FImsvc_recommended_pragmas.h',
        '-EHsc',
        '-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS', # Don't worry about the C++17 deprecations
        '-D__PRETTY_FUNCTION__=__FUNCSIG__',
        '-wd4099',
        '-wd4251',
        '-wd4291',
        '-wd4800',
        '-wd5030',
    ]), language: ['cpp', 'c'])
else
    # Ignore spurious compiler warnings for things that GLib and SpiderMonkey
    # header files commonly do
    add_project_arguments(cxx.get_supported_arguments([
        '-fno-strict-aliasing',
        '-Wno-variadic-macros',  # GLib uses these in header files
        '-Wno-missing-field-initializers',  # SpiderMonkey JSClass, among others
        '-Wno-dangling-pointer',  # Root list in JS::Rooted<T> with GCC 12
    ]), language: 'cpp')

    add_project_arguments(cc.get_supported_arguments([
        '-Wno-typedef-redefinition',  # GLib does this in header files
    ]), language: 'c')
endif

if cc.get_argument_syntax() == 'msvc'
    add_project_arguments(cxx.get_supported_arguments([
        '-Dssize_t=gssize',  # Windows SDK/MSVC headers do not come with ssize_t
        '-DNOMINMAX',  # We don't want 'min' or 'max' to interfere
        '-DSSIZE_MAX=G_MAXSSIZE',  # Windows SDK/MSVC headers do not come with SSIZE_MAX
        ]), language: ['cpp', 'c'])
else
    if get_option('bsymbolic_functions')
        if not cxx.has_link_argument('-Bsymbolic-functions')
            error('''-Bsymbolic-functions not supported, configure with
-Dbsymbolic_functions=false''')
        endif
        add_project_link_arguments('-Bsymbolic-functions', language: ['cpp', 'c'])
        if cc.has_argument('-fno-semantic-interposition')
            add_project_arguments('-fno-semantic-interposition', language: 'c')
        endif
        if cxx.has_argument('-fno-semantic-interposition')
            add_project_arguments('-fno-semantic-interposition', language: 'cpp')
        endif
    endif
endif

# -fno-rtti is not compatible with the vptr sanitizer (part of ubsan)
if not get_option('cpp_rtti') and get_option('b_sanitize') != 'none' and \
    cxx.has_argument('-fno-sanitize=vptr')
    add_project_arguments('-fno-sanitize=vptr', language: 'cpp')
endif

if get_option('verbose_logs')
    add_project_arguments([
        '-DGJS_VERBOSE_ENABLE_PROPS=1',
        '-DGJS_VERBOSE_ENABLE_MARSHAL=1',
        '-DGJS_VERBOSE_ENABLE_LIFECYCLE=1',
        '-DGJS_VERBOSE_ENABLE_GI_USAGE=1',
        '-DGJS_VERBOSE_ENABLE_GCLOSURE=1',
        '-DGJS_VERBOSE_ENABLE_GSIGNAL=1',
    ], language: 'cpp')
endif

if release_build
    add_project_arguments('-DG_DISABLE_CAST_CHECKS', language: ['c', 'cpp'])
endif

### Check for required libraries ###############################################

null_dep = dependency('', required : false)

# Note: Notify GNOME release team when adding or updating dependencies
glib_required_version = '>= 2.86.0'
glib = dependency('glib-2.0', version: glib_required_version, required: false)
gthread = dependency('gthread-2.0', version: glib_required_version, required: false)
gobject = dependency('gobject-2.0', version: glib_required_version, required: false)
gio = dependency('gio-2.0', version: glib_required_version, required: false)
gi = dependency('girepository-2.0', version: glib_required_version, required: false)
gir_deps = []

if not glib.found()
    glib_project = subproject('glib', required: true,
        default_options: ['introspection=enabled'])

    glib = glib_project.get_variable('libglib_dep')
    gthread = glib_project.get_variable('libgthread_dep')
    gobject = glib_project.get_variable('libgobject_dep')
    gio = glib_project.get_variable('libgio_dep')
    gi = glib_project.get_variable('libgirepository_dep')

    gir_deps = [
        glib_project.get_variable('glib_gir'),
        glib_project.get_variable('gobject_gir'),
        glib_project.get_variable('gio_gir'),
        glib_project.get_variable('gio_platform_gir'),
    ]

    if host_machine.system() == 'windows'
        gir_deps += glib_project.get_variable('glib_win32_gir')
    else
        gir_deps += glib_project.get_variable('glib_unix_gir')
    endif
endif

ffi = dependency('libffi', fallback: ['libffi', 'ffi_dep'])
cairo = dependency('cairo', fallback: ['cairo', 'libcairo_dep'])
cairo_gobject = dependency('cairo-gobject',
    fallback: ['cairo', 'libcairogobject_dep'])
cairo_xlib = dependency('cairo-xlib', required: false)
spidermonkey = dependency('mozjs-140')

sysprof_capture = dependency('sysprof-capture-4',
    required: get_option('profiler'), include_type: 'system',
    fallback: ['sysprof', 'libsysprof_capture_dep'],
    default_options: [
        'agent=false',
        'examples=false',
        'gtk=false',
        'tests=false',
        'tools=false',
        'libsysprof=false',
        'sysprofd=none',
        'help=false',
    ])

readline = cxx.find_library('readline', required: get_option('readline'))
# On some systems we need to link readline to a termcap compatible library
readline_code = '''
#include <cstdio>
#include <readline/readline.h>
int main(void) {
    readline("foo");
    return 0;
}'''
readline_deps = [readline]
if readline.found() and not cxx.links(readline_code, dependencies: readline)
    extra_readline_libs = ['ncursesw', 'ncurses', 'curses', 'termcap']
    found = false
    foreach lib : extra_readline_libs
        termcap = cxx.find_library(lib, required: false)
        if cxx.links(readline_code, dependencies: [readline, termcap])
            found = true
            readline_deps += termcap
            break
        endif
    endforeach
    if not found
        error('''Couldn't figure out how to link
readline library. Configure with -Dreadline=disabled to skip the readline
features.''')
    endif
endif

if cxx.links('''
#include <atomic>

int main(void)
{
  std::atomic_int64_t value = ATOMIC_VAR_INIT(0);
  return value.load();
}
''', name: '64-bit atomics built-in')
    libatomic = null_dep
else
    libatomic = cc.find_library('atomic', required: false)
endif

build_profiler = sysprof_capture.found()
profiler_deps = [sysprof_capture]
if build_profiler and not cxx.has_function('timer_settime')
    extra_timer_libs = ['rt', 'posix4']
    found = false
    foreach lib : extra_timer_libs
        timer_lib = cxx.find_library(lib, required: false)
        if cxx.has_function('timer_settime', dependencies: timer_lib)
            found = true
            profiler_deps += timer_lib
            break
        endif
    endforeach
    if not found or not cxx.has_header_symbol('signal.h', 'SIGEV_THREAD_ID')
        if get_option('profiler').enabled()
            error('''The profiler is currently only
supported on Linux. The standard library must support timer_settime() and
SIGEV_THREAD_ID. Configure with -Dprofiler=auto or -Dprofiler=disabled to skip
it on other platforms.''')
        endif
        build_profiler = false
    endif
endif

build_readline = readline.found()

have_gtk3 = dependency('gtk+-3.0', required: false).found()
gtk4_dep = dependency('gtk4', required: false)
have_gtk4 = gtk4_dep.found()

if (not have_gtk3 and not have_gtk4 and not get_option('skip_gtk_tests'))
    error('''You have neither GTK 3 nor 4 available.
GTK is not required, but without at least one of these versions you'll be
skipping a lot of tests. Configure with -Dskip_gtk_tests=true if that's
intentional.''')
endif

### Check for library features #################################################

# Check if SpiderMonkey was compiled with --enable-debug. If this is the case,
# you must compile all your sources with -DDEBUG=1
# See https://bugzilla.mozilla.org/show_bug.cgi?id=1261161
debug_arg = []
nondebug_spidermonkey = cxx.compiles('''
#include <js-config.h>
#ifdef JS_DEBUG
#error debug yes, if we did not already error out due to DEBUG not being defined
#endif
''',
    dependencies: spidermonkey,
    name: 'SpiderMonkey is a non-debug build')

if not nondebug_spidermonkey
    debug_arg = ['-DDEBUG']  # for compile tests
endif

if release_build and not nondebug_spidermonkey
    error('''You are trying to make a release
build with a debug-enabled copy of SpiderMonkey. This is probably not what you
want, since it will have bad performance and is not binary-compatible with
release builds of SpiderMonkey. Try configuring SpiderMonkey with
--disable-debug.''')
endif

# Check if a minimal SpiderMonkey program compiles, links, and runs. If not,
# it's most likely the case that SpiderMonkey was configured incorrectly, for
# example by building mozglue as a shared library.
minimal_program = cxx.run('''
#include <js/Initialization.h>
int main(void) {
    if (!JS_Init()) return 1;
    JS_ShutDown();
    return 0;
}
''',
    args: debug_arg, dependencies: spidermonkey,
    name: 'SpiderMonkey sanity check')

recommended_configuration = ''' Check the recommended configuration:
https://github.com/spidermonkey-embedders/spidermonkey-embedding-examples/blob/esr91/docs/Building%20SpiderMonkey.md'''
if not minimal_program.compiled()
    error('''A minimal SpiderMonkey program
could not be compiled or linked. Most likely you should build it with a
different configuration.''' + recommended_configuration)
elif meson.is_cross_build()
    warning('''This is a cross build. A check that a minimal
SpiderMonkey program executes will not be performed. Before shipping GJS, you
should check that it does not crash on startup, since building SpiderMonkey with
the wrong configuration may cause that.''' + recommended_configuration)
elif minimal_program.returncode() != 0
    error('''A minimal SpiderMonkey program
failed to execute. Most likely you should build it with a different
configuration.''' + recommended_configuration)
endif

### Check for external programs ################################################

dtrace = find_program('dtrace', required: get_option('dtrace'))
dbus_run_session = find_program('dbus-run-session',
    required: not get_option('skip_dbus_tests'))
glib_compile_schemas = find_program('glib-compile-schemas')

### Generate config.h ##########################################################

header_conf = configuration_data()

versions = meson.project_version().split('.')
major_version = versions[0].to_int()
minor_version = versions[1].to_int()
micro_version = versions[2].to_int()
int_version = (major_version * 100 + minor_version) * 100 + micro_version
header_conf.set_quoted('VERSION', meson.project_version())
header_conf.set('GJS_VERSION', int_version,
    description: 'The GJS version as an integer')
header_conf.set_quoted('PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(),
    meson.project_version()))

header_conf.set('ENABLE_PROFILER', build_profiler,
    description: 'Build the profiler')
# COMPAT: SpiderMonkey headers in some places use DEBUG instead of JS_DEBUG
# https://bugzilla.mozilla.org/show_bug.cgi?id=1261161 */
header_conf.set('DEBUG', not nondebug_spidermonkey,
    description: 'SpiderMonkey was compiled with --enable-debug')
header_conf.set('HAVE_DTRACE', get_option('dtrace'),
    description: 'Using dtrace probes')
if build_readline
    header_conf.set('HAVE_READLINE_READLINE_H',
        cxx.check_header('readline/readline.h', prefix: '#include <cstdio>',
            required: readline.found()))
endif
header_conf.set('USE_UNITY_BUILD', get_option('unity'))
header_conf.set('HAVE_SYS_SYSCALL_H', cxx.check_header('sys/syscall.h'))
header_conf.set('HAVE_UNISTD_H', cxx.check_header('unistd.h'))
header_conf.set('HAVE_SIGNAL_H', cxx.check_header('signal.h',
    required: build_profiler))

# enable GNU extensions on systems that have them
header_conf.set('_GNU_SOURCE', 1)

configure_file(output: 'config.h', configuration: header_conf)

### Build dtrace probes ########################################################

if get_option('dtrace')
    probes_header_gen = generator(dtrace, output: '@BASENAME@.h',
        arguments: ['-C', '-h', '-s', '@INPUT@', '-o', '@OUTPUT@'])
    probes_objfile_gen = generator(dtrace, output: '@BASENAME@.o',
        arguments: ['-G', '-s', '@INPUT@', '-o', '@OUTPUT@'])
    probes_header = probes_header_gen.process('gi/gjs_gi_probes.d')
    probes_objfile = probes_objfile_gen.process('gi/gjs_gi_probes.d')
else
    probes_header = []
    probes_objfile = []
endif

tapset_subst = configuration_data({'EXPANDED_LIBDIR': libdir})
tapset = configure_file(input: 'gjs/gjs.stp.in', output: 'gjs.stp',
    configuration: tapset_subst)
if get_option('systemtap')
    install_data(tapset, install_dir: datadir / 'systemtap' / 'tapset')
endif

### Build library ##############################################################

directory_defines = [
    '-DGJS_JS_DIR="@0@"'.format(prefix / gjsjsdir),
    '-DPKGLIBDIR="@0@"'.format(prefix / pkglibdir),
]

gjs_public_headers = [
    'gjs/context.h',
    'gjs/coverage.h',
    'gjs/error-types.h',
    'gjs/gjs.h',
    'gjs/macros.h',
    'gjs/mem.h',
    'gjs/profiler.h',
]

# For historical reasons, some files live in gi/
# Some headers in the following list were formerly public

libgjs_sources = [
    'gi/arg.cpp', 'gi/arg.h', 'gi/arg-inl.h',
    'gi/arg-cache.cpp', 'gi/arg-cache.h',
    'gi/boxed.cpp', 'gi/boxed.h',
    'gi/closure.cpp', 'gi/closure.h',
    'gi/cwrapper.cpp', 'gi/cwrapper.h',
    'gi/enumeration.cpp', 'gi/enumeration.h',
    'gi/foreign.cpp', 'gi/foreign.h',
    'gi/fundamental.cpp', 'gi/fundamental.h',
    'gi/function.cpp', 'gi/function.h',
    'gi/gerror.cpp', 'gi/gerror.h',
    'gi/gjs_gi_trace.h',
    'gi/gobject.cpp', 'gi/gobject.h',
    'gi/gtype.cpp', 'gi/gtype.h',
    'gi/info.h',
    'gi/interface.cpp', 'gi/interface.h',
    'gi/ns.cpp', 'gi/ns.h',
    'gi/object.cpp', 'gi/object.h',
    'gi/param.cpp', 'gi/param.h',
    'gi/private.cpp', 'gi/private.h',
    'gi/repo.cpp', 'gi/repo.h',
    'gi/struct.cpp', 'gi/struct.h',
    'gi/toggle.cpp', 'gi/toggle.h',
    'gi/union.cpp', 'gi/union.h',
    'gi/utils-inl.h',
    'gi/value.cpp', 'gi/value.h',
    'gi/wrapperutils.cpp', 'gi/wrapperutils.h',
    'gjs/atoms.cpp', 'gjs/atoms.h',
    'gjs/auto.h',
    'gjs/byteArray.cpp', 'gjs/byteArray.h',
    'gjs/context.cpp', 'gjs/context-private.h',
    'gjs/coverage.cpp',
    'gjs/debugger.cpp',
    'gjs/deprecation.cpp', 'gjs/deprecation.h',
    'gjs/engine.cpp', 'gjs/engine.h',
    'gjs/error-types.cpp',
    'gjs/gerror-result.h',
    'gjs/global.cpp', 'gjs/global.h',
    'gjs/importer.cpp', 'gjs/importer.h',
    'gjs/internal.cpp', 'gjs/internal.h',
    'gjs/mainloop.cpp', 'gjs/mainloop.h',
    'gjs/mem.cpp', 'gjs/mem-private.h',
    'gjs/module.cpp', 'gjs/module.h',
    'gjs/native.cpp', 'gjs/native.h',
    'gjs/objectbox.cpp', 'gjs/objectbox.h',
    'gjs/profiler.cpp', 'gjs/profiler-private.h',
    'gjs/text-encoding.cpp', 'gjs/text-encoding.h',
    'gjs/promise.cpp', 'gjs/promise.h',
    'gjs/stack.cpp',
    'modules/console.cpp', 'modules/console.h',
    'modules/print.cpp', 'modules/print.h',
    'modules/system.cpp', 'modules/system.h',
    'modules/cairo-private.h',
    'modules/cairo-module.h',
    'modules/cairo-region.cpp',
    'modules/cairo-context.cpp',
    'modules/cairo-path.cpp',
    'modules/cairo-surface.cpp',
    'modules/cairo-image-surface.cpp',
    'modules/cairo-ps-surface.cpp',
    'modules/cairo-pdf-surface.cpp',
    'modules/cairo-svg-surface.cpp',
    'modules/cairo-pattern.cpp',
    'modules/cairo-gradient.cpp',
    'modules/cairo-linear-gradient.cpp',
    'modules/cairo-radial-gradient.cpp',
    'modules/cairo-surface-pattern.cpp',
    'modules/cairo-solid-pattern.cpp',
    'modules/cairo.cpp',
]

# GjsPrivate introspection sources
libgjs_private_sources = [
    'libgjs-private/gjs-gdbus-wrapper.c', 'libgjs-private/gjs-gdbus-wrapper.h',
    'libgjs-private/gjs-match-info.c', 'libgjs-private/gjs-match-info.h',
    'libgjs-private/gjs-util.c', 'libgjs-private/gjs-util.h',
]

libgjs_jsapi_sources = [
    'gjs/jsapi-class.h',
    'gjs/jsapi-dynamic-class.cpp',
    'gjs/jsapi-util-args.h',
    'gjs/jsapi-util-error.cpp',
    'gjs/jsapi-util-root.h',
    'gjs/jsapi-util-string.cpp',
    'gjs/jsapi-util.cpp', 'gjs/jsapi-util.h',
    'util/console.cpp', 'util/console.h',
    'util/log.cpp', 'util/log.h',
    'util/misc.cpp', 'util/misc.h',
]

module_resource_srcs = gnome.compile_resources('js-resources',
    'js.gresource.xml',
    c_name: 'js_resources')
module_resource_lib = static_library('js-resources', module_resource_srcs,
    dependencies: gio, override_options: ['unity=off'])

libgjs_dependencies = [glib, gobject, gthread, gio, gi, ffi, cairo,
    cairo_gobject, spidermonkey, readline, libatomic]
pkg_dependencies = [glib, gobject, gthread, gio, gi, ffi, cairo, cairo_gobject,
    spidermonkey]

if cairo_xlib.found()
    libgjs_dependencies += cairo_xlib
    pkg_dependencies += cairo_xlib
endif

if build_readline
    gio_unix = dependency('gio-unix-2.0', version: glib_required_version,
        fallback: ['glib', 'libgiounix_dep'])
    libgjs_dependencies += [readline_deps, gio_unix]
endif

libgjs_cpp_args = ['-DGJS_COMPILATION'] + directory_defines

# Check G-I and/or Meson on this one.
libgjs_cpp_args += ['-DG_LOG_DOMAIN="Gjs"']

if host_machine.system() == 'windows'
    # We need these defines to build properly for all Windows builds
    libgjs_cpp_args += ['-DWIN32', '-DXP_WIN', '-DWIN32_LEAN_AND_MEAN']
endif

# This dependency should provide everything that is needed to compile gjs except
# the sources themselves, is used to compile both the static libraries and the
# tests
base_build_dep = declare_dependency(
    compile_args: libgjs_cpp_args,
    dependencies: libgjs_dependencies)

internal_build_dep = declare_dependency(
    compile_args: (release_build ? ['-DG_DISABLE_ASSERT'] : []),
    dependencies: [
        base_build_dep,
        build_profiler ? profiler_deps : [],
    ])

libgjs_jsapi = static_library(meson.project_name() + '-jsapi',
    libgjs_jsapi_sources, probes_header, probes_objfile,
    cpp_pch: 'gjs/gjs_pch.hh',
    dependencies: internal_build_dep,
    install: false)

# We need to create an internal static library to be able to link with the tests
# that may use internal APIs. This is also used to generate the actual shared
# library so that we compile its sources just once.
libgjs_internal = static_library('gjs-internal',
    libgjs_sources, probes_header, probes_objfile,
    cpp_pch: 'gjs/gjs_pch.hh',
    dependencies: internal_build_dep,
    link_with: libgjs_jsapi)

link_args = []
symbol_map = files('libgjs.map')
symbol_list = files('libgjs.symbols')  # macOS linker
link_args += cxx.get_supported_link_arguments([
    '-Wl,--version-script,@0@'.format(symbol_map[0].full_path()),
    '-Wl,-exported_symbols_list,@0@'.format(symbol_list[0].full_path()),
])

libgjs = shared_library(meson.project_name(),
    sources: libgjs_private_sources,
    link_args: link_args, link_depends: [symbol_map, symbol_list],
    link_whole: [libgjs_internal, module_resource_lib],
    dependencies: base_build_dep,
    version: '0.0.0', soversion: '0',
    gnu_symbol_visibility: 'hidden',
    install: true)

install_headers(gjs_public_headers, subdir: api_name / 'gjs')

# Allow using libgjs as a subproject
libgjs_dep = declare_dependency(link_with: [libgjs, libgjs_jsapi],
    dependencies: base_build_dep, include_directories: top_include)

### Build GjsPrivate introspection library #####################################

gjs_private_gir = gnome.generate_gir(libgjs,
    includes: ['GObject-2.0', 'Gio-2.0'], sources: libgjs_private_sources,
    namespace: 'GjsPrivate', nsversion: '1.0', identifier_prefix: 'Gjs',
    symbol_prefix: 'gjs_', fatal_warnings: get_option('werror'), install: true,
    install_gir: false, install_dir_typelib: pkglibdir / 'girepository-1.0')
gjs_private_typelib = gjs_private_gir[1]

### Build gjs-console interpreter ##############################################

gjs_console_srcs = ['gjs/console.cpp']

gjs_console = executable('gjs-console', gjs_console_srcs,
    dependencies: libgjs_dep, install: true)

meson.add_install_script('build/symlink-gjs.py', bindir)

### Install data files #########################################################

install_data('installed-tests/extra/gjs.supp', install_dir: gjsjsdir / 'valgrind')
install_data('installed-tests/extra/lsan.supp', install_dir: gjsjsdir / 'lsan')

if get_option('installed_tests')
    schemadir = datadir / 'glib-2.0' / 'schemas'
    install_data('installed-tests/js/org.gnome.GjsTest.gschema.xml', install_dir: schemadir)
    meson.add_install_script(glib_compile_schemas, prefix / schemadir, skip_if_destdir: true)
endif

### Generate pkg-config file ###################################################

requires_private_names = ['gthread-2.0', 'girepository-2.0', 'libffi', 'cairo',
    'cairo-gobject', 'mozjs-140']
if pkg_dependencies.contains(cairo_xlib)
    requires_private_names += 'cairo-xlib'
endif
pkg.generate(libgjs, name: api_name, description: 'JS bindings for GObjects',
    requires: ['glib-2.0', 'gobject-2.0', 'gio-2.0'],
    requires_private: requires_private_names,
    subdirs: api_name,
    variables: [
        'exec_prefix=${prefix}',
        'datarootdir=${datadir}',
        'gjs_console=${bindir}/gjs-console',
        'mozjs_dep_name=@0@'.format(spidermonkey.name()),
    ])

### Test environment ###########################################################

tests_environment = environment()
gi_tests_builddir = meson.project_build_root() / 'subprojects' / 'gobject-introspection-tests'
glib_root_builddir = meson.project_build_root() / 'subprojects' / 'glib'
glib_builddir = glib_root_builddir / 'glib'
gobject_builddir = glib_root_builddir / 'gobject'
gi_builddir = glib_root_builddir / 'girepository'
gio_builddir = glib_root_builddir / 'gio'
js_tests_builddir = meson.current_build_dir() / 'installed-tests' / 'js'
libgjs_test_tools_builddir = js_tests_builddir / 'libgjstesttools'
# GJS_PATH is empty here since we want to force the use of our own
# resources. G_FILENAME_ENCODING ensures filenames are not UTF-8
tests_environment.set('TOP_BUILDDIR', meson.project_build_root())
tests_environment.set('GJS_USE_UNINSTALLED_FILES', '1')
tests_environment.set('GJS_PATH', '')
tests_environment.set('GJS_DEBUG_OUTPUT', 'stderr')
tests_environment.prepend('GI_TYPELIB_PATH', meson.current_build_dir(),
    gi_tests_builddir, js_tests_builddir, libgjs_test_tools_builddir,
    gi_builddir / 'introspection')
tests_environment.prepend('LD_LIBRARY_PATH', meson.current_build_dir(),
    gi_tests_builddir, js_tests_builddir, libgjs_test_tools_builddir,
    glib_builddir, gobject_builddir, gio_builddir, gi_builddir)
tests_environment.prepend('DYLD_LIBRARY_PATH', meson.current_build_dir(),
    gi_tests_builddir, js_tests_builddir, libgjs_test_tools_builddir)
tests_environment.set('G_FILENAME_ENCODING', 'latin1')
# Workaround for https://github.com/google/sanitizers/issues/1322
tests_environment.set('ASAN_OPTIONS', 'intercept_tls_get_addr=0')
lsan_suppressions = files('installed-tests/extra/lsan.supp')
tests_environment.set('LSAN_OPTIONS',
    'fast_unwind_on_malloc=0,exitcode=23,suppressions=@0@'.format(
        lsan_suppressions[0].full_path()))
tsan_suppressions = files('installed-tests/extra/tsan.supp')
tests_environment.set('TSAN_OPTIONS',
    'history_size=5,force_seq_cst_atomics=1,suppressions=@0@'.format(
        tsan_suppressions[0].full_path()))
tests_environment.set('G_SLICE', 'always-malloc')
tests_environment.set('NO_AT_BRIDGE', '1')
tests_environment.set('GTK_A11Y', 'none')
tests_environment.set('GSETTINGS_SCHEMA_DIR', js_tests_builddir)
tests_environment.set('GSETTINGS_BACKEND', 'memory')
tests_environment.set('G_DEBUG', 'fatal-warnings,fatal-criticals')

# Vulkan renderer may not be compatible in all environments e.g CI, use opengl instead
tests_environment.set('GSK_RENDERER',
    gtk4_dep.version().version_compare('>= 4.19.4') ? 'gl' : 'ngl')

tests_locale = 'N/A'
if cxx.get_argument_syntax() != 'msvc'
    result = run_command('build/choose-tests-locale.sh', check: false)
    if result.returncode() == 0
        tests_locale = result.stdout().strip()
        tests_environment.set('LC_ALL', tests_locale)
    endif
endif

if not get_option('skip_gtk_tests')
    tests_environment.set('ENABLE_GTK', 'yes')
endif

if get_option('b_coverage')
    tests_environment.set('GJS_UNIT_COVERAGE_OUTPUT', 'lcov')
    tests_environment.set('GJS_UNIT_COVERAGE_PREFIX',
        'resource:///org/gnome/gjs')
endif

### Tests and test setups ######################################################

# External code should not error out even when building with -Werror
gi_tests = subproject('gobject-introspection-tests',
    default_options: ['werror=false', 'cairo=true',
        'install_dir=@0@'.format(installed_tests_execdir)])

subdir('installed-tests')

# Note: The test program in test/ needs to be ported
#       to Windows before we can build it on Windows.
if host_machine.system() != 'windows'
    subdir('test')
endif

valgrind_environment = environment()
valgrind_environment.set('G_SLICE', 'always-malloc,debug-blocks')
valgrind_environment.set('G_DEBUG',
    'fatal-warnings,fatal-criticals,gc-friendly')
valgrind_environment.set('VALGRIND', 'valgrind')

glib_prefix = glib.get_variable(pkgconfig: 'prefix', default_value: '/usr')
glib_suppresssions = (glib_prefix / 'share' / 'glib-2.0' / 'valgrind' /
    'glib.supp')
gjs_suppressions = files('installed-tests/extra/gjs.supp')
valgrind_args = [
    '--suppressions=@0@'.format(glib_suppresssions),
    '--suppressions=@0@'.format(gjs_suppressions[0].full_path()),
    '--leak-check=full',
    '--num-callers=15',
    '--trace-children=yes',
    '--trace-children-skip=*basename,*cat,*diff,*echo,*grep,*rm,*sed,*stat,*true',
    '--error-exitcode=1'
]

add_test_setup('quiet', env: ['GJS_DEBUG_TOPICS='], is_default: true)
add_test_setup('verbose')

add_test_setup('valgrind', timeout_multiplier: 40, env: valgrind_environment,
    exe_wrapper: ['valgrind'] + valgrind_args)

zeal2_environment = environment()
zeal2_environment.set('JS_GC_ZEAL', '2,10')
add_test_setup('extra_gc', timeout_multiplier: 40, env: zeal2_environment)

zeal4_environment = environment()
zeal4_environment.set('JS_GC_ZEAL', '4')
add_test_setup('pre_verify', timeout_multiplier: 40, env: zeal4_environment)

zeal11_environment = environment()
zeal11_environment.set('JS_GC_ZEAL', '11')
add_test_setup('post_verify', timeout_multiplier: 2, env: zeal11_environment)

### Warn about conditions that may affect runtime ##############################

if tests_locale == 'C' or tests_locale == 'N/A'
    warning('''Your libc does not have the C.UTF-8 locale and no other
suitable UTF-8 fallback locale could be found. You can still build GJS, but
some tests will fail.''')
endif

if get_option('buildtype').startswith('debug') and nondebug_spidermonkey
    warning('''Your copy of SpiderMonkey is not debug-enabled, but you are
building a debug or debugoptimized build. This will make development more
difficult. Consider reconfiguring SpiderMonkey with --enable-debug.''')
endif

if get_option('skip_gtk_tests')
    warning('Not using GTK, not all tests will be run.')
elif not have_gtk3
    warning('Not using GTK 3, not all tests will be run.')
elif not have_gtk4
    warning('Not using GTK 4, not all tests will be run.')
endif

if get_option('skip_dbus_tests')
    warning('Not using DBus, not all tests will be run.')
endif

### Summarize options ##########################################################

summary({
    'prefix': prefix,
    'bindir': prefix / bindir,
    'libdir': prefix / libdir,
    'datadir': prefix / datadir,
    'libexecdir': prefix / libexecdir,
}, section: 'Directories')
locations = []
foreach dep: [ffi, glib, spidermonkey, readline, sysprof_capture]
    if dep.type_name() == 'pkgconfig'
        locations += 'in @0@'.format(dep.get_variable(pkgconfig: 'prefix'))
    else
        locations += dep.type_name()
    endif
endforeach
summary({
    'libffi': '@0@ (@1@)'.format(ffi.version(), locations[0]),
    'GLib': '@0@ (@1@)'.format(glib.version(), locations[1]),
    'SpiderMonkey': '@0@ (@1@, @2@ build)'.format(spidermonkey.version(),
        locations[2], nondebug_spidermonkey ? 'release' : 'debug'),
}, section: 'Dependencies')
if build_readline
    summary('Readline', '(@0@)'.format(locations[3]), section: 'Dependencies')
endif
if build_profiler
    summary('Sysprof',
        '@0@ (@1@)'.format(sysprof_capture.version(), locations[4]),
        section: 'Dependencies')
endif
summary({
    'Build type': get_option('buildtype'),
    'Installed tests': get_option('installed_tests'),
    '-Bsymbolic-functions': get_option('bsymbolic_functions'),
    'Skip DBus tests': get_option('skip_dbus_tests'),
    'Skip GTK tests': get_option('skip_gtk_tests'),
    'Extra debug logs': get_option('verbose_logs'),
    'Precompiled headers': get_option('b_pch'),
}, section: 'Build options', bool_yn: true)
summary({
    'Use readline for input': build_readline,
    'Profiler (Linux only)': build_profiler,
    'Dtrace debugging': get_option('dtrace'),
    'Systemtap debugging': get_option('systemtap'),
}, section: 'Optional features', bool_yn: true)

### Development environment ####################################################

meson.add_devenv({'GJS_USE_UNINSTALLED_FILES': '1'})

### Maintainer scripts #########################################################

run_target('maintainer-tag-release',
  command: ['build/maintainer-tag-release.sh', meson.project_version()])
