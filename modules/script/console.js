// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

/* exported Repl, interact */

var Repl = null;

function interact() {
    const repl = new Repl();

    repl.start();
}
