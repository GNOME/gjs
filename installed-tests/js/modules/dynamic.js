// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh

/* exported test */

async function test() {
    const {say} = await import('./say.js');

    return say('I did it!');
}
