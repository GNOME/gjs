# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>
# SPDX-FileCopyrightText: 2019 Chun-wei Fan <fanchunwei@src.gnome.org>

### Jasmine tests ##############################################################

jsunit_resources_files = gnome.compile_resources('jsunit-resources',
    'jsunit.gresources.xml', c_name: 'jsunit_resources')

minijasmine = executable('minijasmine', '../minijasmine.cpp',
    jsunit_resources_files, dependencies: libgjs_dep,
    cpp_args: [
        '-DINSTTESTDIR="@0@"'.format(prefix / installed_tests_execdir),
    ],
    include_directories: top_include,
    install: get_option('installed_tests'), install_dir: installed_tests_execdir)

subdir('libgjstesttools')

jasmine_tests = [
    'self',
    'Cairo',
    'Exceptions',
    'Format',
    'Fundamental',
    'Gettext',
    'GIMarshalling',
    'Gio',
    'GLib',
    'GObject',
    'GObjectClass',
    'GObjectInterface',
    'GObjectValue',
    'GTypeClass',
    'Importer',
    'Importer2',
    'Introspection',
    'Lang',
    'LegacyByteArray',
    'LegacyClass',
    'LegacyGObject',
    'Mainloop',
    'Namespace',
    'Package',
    'ParamSpec',
    'Print',
    'Promise',
    'Regress',
    'Signals',
    'System',
    'Tweener',
    'WarnLib',
]

if not get_option('skip_gtk_tests')
    jasmine_tests += [
        'GObjectDestructionAccess',
        'LegacyGtk',
    ]

    if have_gtk3
        jasmine_tests += 'Gtk3'
    endif
    if have_gtk4
        jasmine_tests += 'Gtk4'
    endif
endif

installed_js_tests_dir = installed_tests_execdir / 'js'

gschemas_compiled = gnome.compile_schemas(
    depend_files: 'org.gnome.GjsTest.gschema.xml')

tests_dependencies = [
    gschemas_compiled,
    gjs_private_typelib,
    gjstest_tools_typelib,
    gi_tests.get_variable('gimarshallingtests_typelib'),
    gi_tests.get_variable('regress_typelib'),
    gi_tests.get_variable('regress_unix_typelib'),
    gi_tests.get_variable('warnlib_typelib'),
    gi_tests.get_variable('utility_typelib'),
]

foreach test : jasmine_tests
    test_file = files('test@0@.js'.format(test))

    test(test, minijasmine, args: test_file, depends: tests_dependencies,
        env: tests_environment, protocol: 'tap', suite: 'JS')

    test_description_subst = {
        'name': 'test@0@.js'.format(test),
        'installed_tests_execdir': prefix / installed_tests_execdir,
    }
    configure_file(configuration: test_description_subst,
        input: '../minijasmine.test.in',
        output: 'test@0@.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: installed_tests_metadir)

    if get_option('installed_tests')
        install_data(test_file, install_dir: installed_js_tests_dir)
    endif
endforeach

if get_option('installed_tests')
    install_subdir('modules', install_dir: installed_js_tests_dir)
endif

# testGDBus.js is separate, because it can be skipped, and
# during build should be run using dbus-run-session
bus_config = files('../../test/test-bus.conf')
dbus_test_file = files('testGDBus.js')

if not get_option('skip_dbus_tests')
    test('GDBus', dbus_run_session,
        args: ['--config-file', bus_config, '--', minijasmine, dbus_test_file],
        env: tests_environment, protocol: 'tap', suite: 'dbus',
        depends: tests_dependencies)
endif

dbus_test_description_subst = {
    'name': 'testGDBus.js',
    'installed_tests_execdir': prefix / installed_tests_execdir,
}
configure_file(
    configuration: dbus_test_description_subst,
    input: '../minijasmine.test.in',
    output: 'testGDBus.test',
    install: get_option('installed_tests'),
    install_dir: installed_tests_metadir)

if get_option('installed_tests')
    install_data(dbus_test_file, install_dir: installed_js_tests_dir)
endif

# tests using ES modules are also separate because they need an extra
# minijasmine flag

modules_tests = [
    'Async',
    'CairoModule',
    'Console',
    'ESModules',
    'AsyncMainloop',
    'Encoding',
    'GLibLogWriter',
    'Global',
    'Timers',
    'Utility',
    'WeakRef',
]

foreach test : modules_tests
    test_file = files('test@0@.js'.format(test))

    test(test, minijasmine, args: [test_file, '-m'], env: tests_environment,
        protocol: 'tap', suite: 'JS')

    esm_test_description_subst = {
        'name': 'test@0@.js'.format(test),
        'installed_tests_execdir': prefix / installed_tests_execdir,
    }
    configure_file(configuration: esm_test_description_subst,
        input: '../minijasmine-module.test.in',
        output: 'test@0@.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: installed_tests_metadir)

    if get_option('installed_tests')
        install_data(test_file, install_dir: installed_js_tests_dir)
    endif
endforeach

if get_option('installed_tests')
    install_data('matchers.js', install_dir: installed_js_tests_dir)
endif
