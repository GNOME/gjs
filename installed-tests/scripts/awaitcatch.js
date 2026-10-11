// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

// this JS script should not cause an unhandled promise rejection
async function foo() { // eslint-disable-line require-await
    throw new Error('foo');
}
async function bar() {
    try {
        await foo();
    } catch {}
}
bar();
