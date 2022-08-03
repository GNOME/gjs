// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// This example demonstrates that Promises always execute prior
// to timeouts. It should log "hello" then "world".

import GLib from 'gi://GLib';

const loop = GLib.MainLoop.new(null, false);

const promise = new Promise(r => {
    let i = 100;
    while (i--)
        ;

    r();
});

setTimeout(() => {
    promise.then(() => log('hello'));
});

setTimeout(() => {
    log('world');
});

loop.run();
