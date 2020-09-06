# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

### Unit tests #################################################################

mock_js_resources_files = gnome.compile_resources('mock-js-resources',
    'mock-js-resources.gresource.xml', c_name: 'mock_js_resources',
    source_dir: '..')

gjs_tests_sources = [
    'gjs-tests.cpp',
    'gjs-test-common.cpp', 'gjs-test-common.h',
    'gjs-test-utils.cpp', 'gjs-test-utils.h',
    'gjs-test-call-args.cpp',
    'gjs-test-coverage.cpp',
    'gjs-test-rooting.cpp',
    'gjs-test-no-introspection-object.cpp', 'gjs-test-no-introspection-object.h',
]

gjs_tests = executable('gjs-tests', gjs_tests_sources, mock_js_resources_files,
    cpp_args: ['-DGJS_COMPILATION'] + directory_defines,
    include_directories: top_include, dependencies: libgjs_dep)

test('API tests', gjs_tests, args: ['--tap', '--keep-going', '--verbose'],
    depends: gjs_private_typelib, env: tests_environment, protocol: 'tap',
    suite: 'C')
