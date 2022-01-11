# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

gjstest_tools_sources = [
    'gjs-test-tools.cpp',
    'gjs-test-tools.h',
]
libgjstesttools = library('gjstesttools',
    sources: gjstest_tools_sources,
    include_directories: top_include, dependencies: libgjs_dep,
    cpp_args: libgjs_cpp_args + test_gir_extra_c_args + test_gir_warning_c_args,
    install: get_option('installed_tests'), install_dir: installed_tests_execdir)
gjstest_tools_gir = gnome.generate_gir(libgjstesttools,
    includes: ['GObject-2.0', 'Gio-2.0'], sources: gjstest_tools_sources,
    namespace: 'GjsTestTools', nsversion: '1.0',
    symbol_prefix: 'gjs_test_tools_', extra_args: '--warn-error',
    install: get_option('installed_tests'), install_dir_gir: false,
    install_dir_typelib: installed_tests_execdir)
gjstest_tools_typelib = gjstest_tools_gir[1]
libgjstesttools_dep = declare_dependency(
    link_with: libgjstesttools,
    include_directories: include_directories('.'))
