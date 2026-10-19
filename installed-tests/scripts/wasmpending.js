// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

// This leaves a WebAssembly compilation pending at context destroy time. It
// must be released back to SpiderMonkey, or this test will hang.

import System from 'system';

// Minimal WASM module, just magic + version
const wasmBytes = new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]);
WebAssembly.compile(wasmBytes);

if (System.programArgs[0] === 'exit')
    System.exit(42);
