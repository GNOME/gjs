// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
function foo() {
    print('Print me');
    debugger;
    print('Print me also');
}

function bar() {
    print('Print me');
    debugger;
    print('Print me also');
    return 5;
}

foo();
bar();
print('Print me at the end');
