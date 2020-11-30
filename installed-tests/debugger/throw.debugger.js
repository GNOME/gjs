// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
function a() {
    debugger;
    return 5;
}

try {
    a();
} catch (e) {
    print(`Exception: ${e}`);
}
