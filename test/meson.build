# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

### Unit tests #################################################################

mock_js_resources_files = gnome.compile_resources('mock-js-resources',
    'mock-js-resources.gresource.xml', c_name: 'mock_js_resources',
    source_dir: '..')

libgjs_tests_common = static_library('gjs-tests-common',
    sources: [
        'gjs-test-utils.cpp', 'gjs-test-utils.h',
        'gjs-test-common.cpp', 'gjs-test-common.h',
    ],
    cpp_args: libgjs_cpp_args,
    include_directories: top_include, dependencies: libgjs_dependencies,
)

gjs_tests_sources = [
    'gjs-tests.cpp',
    'gjs-test-coverage.cpp',
    'gjs-test-no-introspection-object.cpp', 'gjs-test-no-introspection-object.h',
]

gjs_tests = executable('gjs-tests', gjs_tests_sources, mock_js_resources_files,
    include_directories: top_include, dependencies: libgjs_dep,
    link_whole: [module_resource_lib],
    link_with: [libgjs_tests_common, libgjs_internal])

test('API tests', gjs_tests, args: ['--tap', '--keep-going', '--verbose'],
    depends: gjs_private_typelib, env: tests_environment, protocol: 'tap',
    suite: 'C', timeout: 60, priority: 10)

gjs_tests_internal = executable('gjs-tests-internal',
    sources: [
        'gjs-tests-internal.cpp',
        'gjs-test-call-args.cpp',
        'gjs-test-rooting.cpp',
        'gjs-test-jsapi-utils.cpp',
        'gjs-test-toggle-queue.cpp',
        module_resource_srcs,
    ],
    include_directories: top_include,
    cpp_args: libgjs_cpp_args,
    dependencies: [libgjs_dependencies, libgjstesttools_dep],
    link_with: [libgjs_tests_common, libgjs_internal])

test('Internal API tests', gjs_tests_internal,
    args: ['--tap', '--keep-going', '--verbose'],
    env: tests_environment, protocol: 'tap',
    suite: 'C', priority: 10)
