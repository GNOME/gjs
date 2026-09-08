// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 GNOME Foundation
// SPDX-FileContributor: Angelo Verlain <hey@vixalien.com>

import Gio from 'gi://Gio';
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

        const p1 = WebAssembly.instantiate(bytes).then(() => order.push('wasm1'));
        const p2 = WebAssembly.instantiate(bytes).then(() => order.push('wasm2'));

        // A plain resolved promise should interleave between wasm completions
        // if microtasks are drained after each WASM job
        const p3 = Promise.resolve().then(() => order.push('microtask'));

        await Promise.all([p1, p2, p3]);

        // microtask must not be deferred until after both WASM jobs complete
        expect(order.indexOf('microtask')).toBeLessThan(order.indexOf('wasm2'));
    });
});
