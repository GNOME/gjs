// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip@endlessm.com>

// This JS script should exit immediately with code 42. If that is not working,
// then it will exit after 3 seconds as a fallback, with code 0.
const GLib = imports.gi.GLib;
let loop = GLib.MainLoop.new(null, false);
GLib.idle_add_once(GLib.PRIORITY_LOW, () => imports.system.exit(42));
GLib.timeout_add_seconds_once(GLib.PRIORITY_HIGH, 3, () => loop.quit());
loop.run();
