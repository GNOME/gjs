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

describe('FinalizationRegistry', function () {
    let registry, callback;
    beforeEach(function () {
        callback = jasmine.createSpy('FinalizationRegistry callback');
        registry = new FinalizationRegistry(callback);
    });

    it('works', function () {
        let obj = {};
        registry.register(obj, 'marker');
        obj = null;
        System.gc();
        PromiseInternal.drainMicrotaskQueue();
        expect(callback).toHaveBeenCalledOnceWith('marker');
    });

    it('works if a microtask is enqueued from the callback', function () {
        let obj = {};
        let secondCallback = jasmine.createSpy('async callback');
        callback.and.callFake(function () {
            return Promise.resolve().then(secondCallback);
        });
        registry.register(obj);
        obj = null;
        System.gc();
        PromiseInternal.drainMicrotaskQueue();
        expect(callback).toHaveBeenCalled();
        expect(secondCallback).toHaveBeenCalled();
    });

    it('works if the object is collected in a microtask', async function () {
        let obj = {};
        registry.register(obj, 'marker');
        await Promise.resolve();
        obj = null;
        System.gc();
        await Promise.resolve();
        expect(callback).toHaveBeenCalled();
    });

    it('works if another collection is queued from the callback', function () {
        let obj = {};
        let obj2 = {};
        callback.and.callFake(function () {
            obj2 = null;
            System.gc();
        });
        registry.register(obj, 'marker');
        registry.register(obj2, 'marker2');
        obj = null;
        System.gc();
        PromiseInternal.drainMicrotaskQueue();
        expect(callback).toHaveBeenCalledTimes(2);
    });
});
