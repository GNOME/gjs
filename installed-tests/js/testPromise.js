// -*- mode: js; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileContributor: Authored by: Marco Trevisan <marco.trevisan@canonical.com>
// SPDX-FileCopyrightText: 2022 Canonical, Ltd.

import GLib from 'gi://GLib';

class SubPromise extends Promise {
    constructor(executor) {
        super((resolve, reject) => {
            executor(resolve, reject);
        });
    }
}

const PromiseResult = {
    RESOLVED: 0,
    FAILED: 1,
};

describe('Promise', function () {
    let loop;

    beforeEach(function () {
        loop = GLib.MainLoop.new(null, false);
    });

    function executePromise(promise) {
        const promiseResult = {};

        promise.then(value => {
            promiseResult.result = PromiseResult.RESOLVED;
            promiseResult.value = value;
        }).catch(e => {
            promiseResult.result = PromiseResult.FAILED;
            promiseResult.error = e;
        });

        while (promiseResult.result === undefined)
            loop.get_context().iteration(true);

        return promiseResult;
    }

    it('waits for all promises before handling unhandled, when handled', function () {
        let error;
        let resolved;

        const promise = async () => {
            await new SubPromise(resolve => resolve('success'));
            const rejecting = new SubPromise((_resolve, reject) => reject('got error'));
            try {
                await rejecting;
            } catch (e) {
                error = e;
            } finally {
                resolved = true;
            }

            expect(resolved).toBeTrue();
            expect(error).toBe('got error');

            return 'parent-returned';
        };

        expect(executePromise(promise())).toEqual({
            result: PromiseResult.RESOLVED,
            value: 'parent-returned',
        });
    });

    it('waits for all promises before handling unhandled, when unhandled', function () {
        const thenHandler = jasmine.createSpy('thenHandler');

        const promise = async () => {
            await new Promise(resolve => resolve('success'));
            await new Promise((_resolve, reject) => reject(new Error('got error')));
            return 'parent-returned';
        };

        promise().then(thenHandler).catch();
        expect(thenHandler).not.toHaveBeenCalled();

        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => loop.quit());
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'Unhandled promise rejection.*');
        loop.run();
        GLib.test_assert_expected_messages_internal('Gjs', 'testPromise.js', 0,
            'warnsIfRejected');
    });

    it('do not lead to high-priority IDLE starvation', function () {
        const promise = new Promise(resolve => {
            const id = GLib.idle_add(GLib.PRIORITY_HIGH, () => {
                resolve();
                return GLib.SOURCE_REMOVE;
            });
            GLib.Source.set_name_by_id(id, `Test Idle source ${id}`);
        });

        expect(executePromise(promise)).toEqual({
            result: PromiseResult.RESOLVED,
            value: undefined,
        });
    });
});
