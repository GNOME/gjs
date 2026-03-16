# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Angelo Verlain <hey@vixalien.com>

wat2wasm = find_program('wat2wasm')

wasm_add = custom_target('wasm-add',
    input: 'add.wat',
    output: 'add.wasm',
    command: [
        wat2wasm,
        '@INPUT@',
        '-o', '@OUTPUT@',
    ],
    build_by_default: true,
    install: get_option('installed_tests'),
    install_dir: installed_js_tests_dir / 'wasm',
)
