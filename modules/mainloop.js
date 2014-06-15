/* -*- mode: js; indent-tabs-mode: nil; -*- */
// Copyright (c) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

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
	throw new Error("No main loop with this id");

    let loop = _mainLoops[name];
    delete _mainLoops[name];

    if (!loop.is_running())
	throw new Error("Main loop was stopped already");

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
