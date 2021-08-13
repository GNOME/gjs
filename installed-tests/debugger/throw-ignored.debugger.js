// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Florian Müllner <fmuellner@gnome.org>

let count = 0;

function a() {
    throw new Error(`Exception nº ${++count}`);
}

try {
    a();
} catch (e) {
    print(`Caught exception: ${e}`);
}

a();
