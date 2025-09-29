/* -*- mode: js; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Giovanni Campagna <scampa.giovanni@gmail.com>

/* exported idle_add, idle_source, quit, run, source_remove, timeout_add,
timeout_add_seconds, timeout_seconds_source, timeout_source */

// A layer of convenience and backwards-compatibility over GLib MainLoop facilities

const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;

var _mainLoops = {};

function run(name) {
    if (!_mainLoops[name])
        _mainLoops[name] = GLib.MainLoop.new(null, false);

    _mainLoops[name].run();
}

function quit(name) {
    if (!_mainLoops[name])
        throw new Error('No main loop with this id');

    let loop = _mainLoops[name];
    _mainLoops[name] = null;

    if (!loop.is_running())
        throw new Error('Main loop was stopped already');

    loop.quit();
}

function idle_source(handler, priority) {
    let s = GLib.idle_source_new();
    GObject.source_set_closure(s, handler);
    if (priority !== undefined)
        s.set_priority(priority);
    return s;
}

function idle_add(handler, priority) {
    return idle_source(handler, priority).attach(null);
}

function timeout_source(timeout, handler, priority) {
    let s = GLib.timeout_source_new(timeout);
    GObject.source_set_closure(s, handler);
    if (priority !== undefined)
        s.set_priority(priority);
    return s;
}

function timeout_seconds_source(timeout, handler, priority) {
    let s = GLib.timeout_source_new_seconds(timeout);
    GObject.source_set_closure(s, handler);
    if (priority !== undefined)
        s.set_priority(priority);
    return s;
}

function timeout_add(timeout, handler, priority) {
    return timeout_source(timeout, handler, priority).attach(null);
}

function timeout_add_seconds(timeout, handler, priority) {
    return timeout_seconds_source(timeout, handler, priority).attach(null);
}

function source_remove(id) {
    return GLib.source_remove(id);
}
