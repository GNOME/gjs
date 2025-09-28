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
    'Async',
    'AsyncMainloop',
    'Cairo',
    'Console',
    'Encoding',
    'ESModules',
    'Exceptions',
    'Fundamental',
    'Gettext',
    'GIMarshalling',
    'Gio',
    'GLib',
    'GLibLogWriter',
    'Global',
    'GObject',
    'GObjectClass',
    'GObjectInterface',
    'GObjectValue',
    'GTypeClass',
    'Introspection',
    'Namespace',
    'ParamSpec',
    'Print',
    'Promise',
    'Regress',
    'System',
    'Timers',
    'Utility',
    'WarnLib',
    'WeakRef',
]

if not get_option('skip_gtk_tests')
    jasmine_tests += 'GObjectDestructionAccess'

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
    gir_deps,
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

    test(test, minijasmine, args: [test_file, '-m'],
        depends: tests_dependencies, env: tests_environment, protocol: 'tap',
        suite: 'JS')

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
        args: [
            '--config-file', bus_config,
            '--',
            minijasmine, dbus_test_file, '-m'
        ],
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

# tests that require the legacy importer are also separate because they need to
# invoke minijasmine without the -m flag

legacy_importer_tests = [
    'Format',
    'Importer',
    'Importer2',
    'Lang',
    'LegacyByteArray',
    'LegacyCairo',
    'LegacyClass',
    'LegacyGObject',
    'Mainloop',
    'Overrides',
    'Package',
    'Signals',
    'Tweener',
]

if not get_option('skip_gtk_tests')
    legacy_importer_tests += 'LegacyGtk'
endif

foreach test : legacy_importer_tests
    test_file = files('test@0@.js'.format(test))

    test(test, minijasmine, args: test_file, depends: tests_dependencies,
        env: tests_environment, protocol: 'tap', suite: 'JS')

    esm_test_description_subst = {
        'name': 'test@0@.js'.format(test),
        'installed_tests_execdir': prefix / installed_tests_execdir,
    }
    configure_file(configuration: esm_test_description_subst,
        input: '../minijasmine-legacy-importer.test.in',
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
