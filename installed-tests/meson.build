# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>
# SPDX-FileCopyrightText: 2019 Chun-wei Fan <fanchunwei@src.gnome.org>

### Installed tests ############################################################

installed_tests_execdir = get_option('prefix') / get_option('libexecdir') / 'installed-tests' / meson.project_name()
installed_tests_metadir = abs_datadir / 'installed-tests' / meson.project_name()

# Simple shell script tests #

simple_tests = []

# The test scripts need to be ported from shell scripts
# for clang-cl builds, which do not use BASH-style shells
if cxx.get_argument_syntax() != 'msvc'
    simple_tests += [
        'CommandLine',
        'CommandLineModules',
        'Warnings',
    ]
endif

foreach test : simple_tests
    test_file = files('scripts' / 'test@0@.sh'.format(test))

    test(test, test_file, env: tests_environment, protocol: 'tap',
         suite: 'Scripts')

    test_description_subst = {
        'name': 'test@0@.sh'.format(test),
        'installed_tests_execdir': installed_tests_execdir,
    }
    test_description = configure_file(configuration: test_description_subst,
        input: 'script.test.in', output: 'test@0@.sh.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: installed_tests_metadir)

    if get_option('installed_tests')
        install_data(test_file, install_dir: installed_tests_execdir / 'scripts')
    endif
endforeach

# Jasmine tests #

subdir('js')

# Debugger script tests #

debugger_tests = [
    'backtrace',
    'breakpoint',
    'continue',
    'delete',
    'detach',
    'down-up',
    'finish',
    'frame',
    'keys',
    'lastvalues',
    'list',
    'next',
    'print',
    'quit',
    'return',
    'set',
    'step',
    'throw',
    'throw-ignored',
    'until',
]

debugger_test_driver = find_program(files('debugger-test.sh'))
if get_option('installed_tests')
    install_data('debugger-test.sh', install_dir: installed_tests_execdir)
endif

foreach test : debugger_tests
    test_file = files('debugger' / '@0@.debugger'.format(test))

    test('@0@ command'.format(test), debugger_test_driver,
        args: test_file, env: tests_environment, protocol: 'tap',
        suite: 'Debugger')

    test_description_subst = {
        'name': '@0@.debugger'.format(test),
        'installed_tests_execdir': installed_tests_execdir,
    }
    test_description = configure_file(configuration: test_description_subst,
        input: 'debugger.test.in',
        output: '@0@.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: installed_tests_metadir)

    if get_option('installed_tests')
        install_data(test_file, install_dir: installed_tests_execdir / 'debugger')
        install_data('debugger' / '@0@.debugger.js'.format(test),
            'debugger' / '@0@.debugger.output'.format(test),
            install_dir: installed_tests_execdir / 'debugger')
    endif
endforeach
