// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 GNOME Foundation
// SPDX-FileContributor: Angelo Verlain <hey@vixalien.com>

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

const wasmFile = Gio.File.new_for_uri('resource:///org/gjs/jsunit/wasm/add.wasm');

describe('WebAssembly', function () {
    it('can instantiate a module', async function () {
        const [, bytes] = wasmFile.load_contents(null);
        const {instance} = await WebAssembly.instantiate(bytes);
        expect(instance).toBeInstanceOf(WebAssembly.Instance);
    });

    it('can call an exported function', async function () {
        const [, bytes] = wasmFile.load_contents(null);
        const {instance} = await WebAssembly.instantiate(bytes);
        expect(instance.exports.add(2, 3)).toBe(5);
    });

    it('runs promise microtasks after each WASM job', async function () {
        const [, bytes] = wasmFile.load_contents(null);

        const order = [];

        async function addMicrotask(name) {
            order.push(name);
            await Promise.resolve();
            // A plain resolved promise should interleave between wasm
            // completions if microtasks are drained after each WASM job
            order.push(`${name}-microtask`);
        }
        const pA = WebAssembly.instantiate(bytes).then(() => addMicrotask('wasmA'));
        const pB = WebAssembly.instantiate(bytes).then(() => addMicrotask('wasmB'));

        // Block the main thread so the two jobs are dispatched in one batch
        GLib.usleep(1e6);

        await Promise.all([pA, pB]);

        // The WASM microtasks run in completion order, not launch order, so not
        // deterministic. Each Promise microtask should run immediately after
        // the WASM microtask that queued it
        expect(order.indexOf('wasmA-microtask')).toBe(order.indexOf('wasmA') + 1);
        expect(order.indexOf('wasmB-microtask')).toBe(order.indexOf('wasmB') + 1);
    });
});
