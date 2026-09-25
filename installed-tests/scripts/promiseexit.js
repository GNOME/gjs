// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Philip Chimento <philip.chimento@gmail.com>

// this is similar to exit.js but should exit with an unhandled promise
// rejection
const {GLib} = imports.gi;
const System = imports.system;
const loop = GLib.MainLoop.new(null, false);
Promise.reject(); // eslint-disable-line prefer-promise-reject-errors
GLib.idle_add_once(GLib.PRIORITY_LOW, () => System.exit(42));
GLib.timeout_add_seconds_once(GLib.PRIORITY_HIGH, 3, () => loop.quit());
loop.run();
