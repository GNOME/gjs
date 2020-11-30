// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
function a() {
    debugger;
    b();
    print('A line in a');
}

function b() {
    print('A line in b');
}

a();
