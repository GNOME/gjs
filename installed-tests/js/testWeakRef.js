// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Philip Chimento <philip.chimento@gmail.com>

import System from 'system';

const PromiseInternal = imports._promiseNative;

describe('WeakRef', function () {
    it('works', function () {
        let obj = {};
        const weakRef = new WeakRef(obj);
        expect(weakRef.deref()).toBe(obj);
        obj = null;

        // Do not use this in real code to process microtasks. This is only for
        // making the test execute synchronously. Instead, in real code, return
        // control to the event loop, e.g. with setTimeout().
        PromiseInternal.drainMicrotaskQueue();
        System.gc();

        expect(weakRef.deref()).not.toBeDefined();
    });
});
