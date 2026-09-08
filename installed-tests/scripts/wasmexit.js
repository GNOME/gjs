// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

// similar to promise.js, but for WASM microtasks not dispatched by a regular
// Promise.

import GLib from 'gi://GLib';
import System from 'system';

// Minimal WASM module, just magic + version
const wasmBytes = new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]);
// WASM compilation jobs are dispatched in completion order, not launch order,
// so we don't know which will execute first
let exited = false;
function finish() {
    if (exited) {
        print('Should not be printed');
        return;
    }
    exited = true;
    print('Should be printed');
    System.exit(42);
}
WebAssembly.instantiate(wasmBytes).then(finish);
WebAssembly.instantiate(wasmBytes).then(finish);
// Block the main thread so the two jobs are dispatched in one batch
GLib.usleep(1e6);
