# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

libgjstesttools_extra_cpp_args = []
if cc.get_argument_syntax() == 'msvc'
  # We need to ensure the symbols in the test DLLs export in clang-cl builds
  libgjstesttools_extra_cpp_args += ['-DGJS_TEST_TOOL_EXTERN=__declspec(dllexport)extern']
endif

gjstest_tools_sources = [
    'gjs-test-tools.cpp',
    'gjs-test-tools.h',
]
libgjstesttools = library('gjstesttools',
    sources: gjstest_tools_sources,
    include_directories: top_include, dependencies: libgjs_dep,
    cpp_args: libgjs_cpp_args + libgjstesttools_extra_cpp_args,
    install: get_option('installed_tests'), install_dir: installed_tests_execdir)
gjstest_tools_gir = gnome.generate_gir(libgjstesttools,
    includes: ['GObject-2.0', 'Gio-2.0'], sources: gjstest_tools_sources,
    namespace: 'GjsTestTools', nsversion: '1.0',
    symbol_prefix: 'gjs_test_tools_', fatal_warnings: get_option('werror'),
    install: get_option('installed_tests'), install_gir: false,
    install_dir_typelib: installed_tests_execdir)
gjstest_tools_typelib = gjstest_tools_gir[1]
libgjstesttools_dep = declare_dependency(
    link_with: libgjstesttools,
    include_directories: include_directories('.'))
