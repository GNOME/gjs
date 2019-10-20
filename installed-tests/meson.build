### Installed tests ############################################################

installed_tests_dir = pkglibexecdir / 'installed-tests'
test_descriptions_dir = (get_option('datadir') / 'installed-tests' /
    meson.project_name())

# Simple shell script tests #

simple_tests = [
    'CommandLine',
    'Warnings',
]

foreach test : simple_tests
    test_file = files('scripts' / 'test@0@.sh'.format(test))

    test(test, test_file, env: tests_environment, protocol: 'tap',
        suite: 'Scripts')

    test_description_subst = {
        'name': 'test@0@.sh'.format(test),
        'pkglibexecdir': abs_pkglibexecdir,
    }
    test_description = configure_file(configuration: test_description_subst,
        input: 'script.test.in', output: 'test@0@.sh.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: test_descriptions_dir)

    if get_option('installed_tests')
        install_data(test_file, install_dir: installed_tests_dir / 'scripts')
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
    'next',
    'print',
    'quit',
    'return',
    'set',
    'step',
    'throw',
    'until',
]

debugger_test_driver = find_program(files('debugger-test.sh'))
if get_option('installed_tests')
    install_data('debugger-test.sh', install_dir: installed_tests_dir)
endif

foreach test : debugger_tests
    test_file = files('debugger' / '@0@.debugger'.format(test))

    test('@0@ command'.format(test), debugger_test_driver,
        args: test_file, env: tests_environment, protocol: 'tap',
        suite: 'Debugger')

    test_description_subst = {
        'name': '@0@.debugger'.format(test),
        'pkglibexecdir': abs_pkglibexecdir,
    }
    test_description = configure_file(configuration: test_description_subst,
        input: 'debugger.test.in',
        output: '@0@.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: test_descriptions_dir)

    if get_option('installed_tests')
        install_data(test_file, install_dir: installed_tests_dir / 'debugger')
        install_data('debugger' / '@0@.debugger.js'.format(test),
            'debugger' / '@0@.debugger.output'.format(test),
            install_dir: installed_tests_dir / 'debugger')
    endif
endforeach
