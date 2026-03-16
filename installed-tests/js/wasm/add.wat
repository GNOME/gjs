;; SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
;; SPDX-FileCopyrightText: 2026 Angelo Verlain <hey@vixalien.com>

(module
    (func $add (param $a i32) (param $b i32) (result i32)
        local.get $a
        local.get $b
        i32.add)

    (export "add" (func $add)))
