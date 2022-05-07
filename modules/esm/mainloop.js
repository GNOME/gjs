// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import GLib from 'gi://GLib';

const {idle_source} = imports.mainloop;

export const mainloop = GLib.MainLoop.new(null, false);

/**
 * Run the mainloop asynchronously
 *
 * @returns {Promise<void>}
 */
export function run() {
    return mainloop.runAsync();
}

/**
 * Quit the mainloop
 */
export function quit() {
    if (!mainloop.is_running())
        throw new Error('Main loop was stopped already');

    mainloop.quit();
}

/**
 * Adds an idle task to the mainloop
 *
 * @param {(...args) => boolean} handler a callback to call when no higher priority events remain
 * @param {number} [priority] the priority to queue the task with
 */
export function idle(handler, priority) {
    const source = idle_source(handler, priority);
    source.attach(null);

    return source;
}

export default {
    mainloop,
    idle,
    run,
    quit,
};
