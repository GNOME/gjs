// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018-2019 the Deno authors. All rights reserved.
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

// Derived from https://github.com/denoland/deno/blob/eda6e58520276786bd87e411d0284eb56d9686a6/cli/tests/unit/timers_test.ts

import GLib from 'gi://GLib';

function deferred() {
    let resolve_;
    let reject_;

    function resolve() {
        resolve_();
    }

    function reject() {
        reject_();
    }

    const promise = new Promise((res, rej) => {
        resolve_ = res;
        reject_ = rej;
    });

    return {
        promise,
        resolve,
        reject,
    };
}

/**
 * @param {number} ms the number of milliseconds to wait
 * @returns {Promise<void>}
 */
function waitFor(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * @param {(resolve?: () => void, reject?: () => void) => void} callback a callback to call with handlers once the promise executes
 * @returns {jasmine.AsyncMatchers}
 */
function expectPromise(callback) {
    return expectAsync(
        new Promise((resolve, reject) => {
            callback(resolve, reject);
        })
    );
}

describe('Timers', () => {
    it('times out successfully', async function () {
        const startTime = GLib.get_monotonic_time();
        const ms = 500;
        let count = 0;
        let endTime;

        await expectPromise(resolve => {
            setTimeout(() => {
                endTime = GLib.get_monotonic_time();
                count++;

                resolve();
            }, ms);
        }).toBeResolved();

        expect(count).toBe(1);
        expect(endTime - startTime).toBeGreaterThanOrEqual(ms);

        return 5;
    });

    it('has correct timeout args', async function () {
        const arg = 1;

        await expectPromise(resolve => {
            setTimeout(
                (a, b, c) => {
                    expect(a).toBe(arg);
                    expect(b).toBe(arg.toString());
                    expect(c).toEqual(jasmine.arrayWithExactContents([arg]));

                    resolve();
                },
                10,
                arg,
                arg.toString(),
                [arg]
            );
        }).toBeResolved();
    });

    it('cancels successfully', async function () {
        let count = 0;
        const timeout = setTimeout(() => {
            count++;
        }, 1);
        // Cancelled, count should not increment
        clearTimeout(timeout);

        await waitFor(600);

        expect(count).toBe(0);
    });

    it('cancels multiple correctly', async function () {
        const uncalled = jasmine.createSpy('uncalled');

        // Set timers and cancel them in the same order.
        const t1 = setTimeout(uncalled, 10);
        const t2 = setTimeout(uncalled, 10);
        const t3 = setTimeout(uncalled, 10);
        clearTimeout(t1);
        clearTimeout(t2);
        clearTimeout(t3);

        // Set timers and cancel them in reverse order.
        const t4 = setTimeout(uncalled, 20);
        const t5 = setTimeout(uncalled, 20);
        const t6 = setTimeout(uncalled, 20);
        clearTimeout(t6);
        clearTimeout(t5);
        clearTimeout(t4);

        // Sleep until we're certain that the cancelled timers aren't gonna fire.
        await waitFor(50);

        expect(uncalled).not.toHaveBeenCalled();
    });

    it('cancels invalid silent fail', async function () {
        // Expect no panic
        const {promise, resolve} = deferred();
        let count = 0;
        const id = setTimeout(() => {
            count++;
            // Should have no effect
            clearTimeout(id);
            resolve();
        }, 500);
        await promise;
        expect(count).toBe(1);

        // Should silently fail (no panic)
        clearTimeout(2147483647);
    });

    it('interval success', async function () {
        const {promise, resolve} = deferred();
        let count = 0;
        const id = setInterval(() => {
            count++;
            clearInterval(id);
            resolve();
        }, 100);
        await promise;
        // Clear interval
        clearInterval(id);
        // count should increment twice
        expect(count).toBe(1);
    });

    it('cancels interval successfully', async function () {
        let count = 0;
        const id = setInterval(() => {
            count++;
        }, 1);
        clearInterval(id);
        await waitFor(500);
        expect(count).toBe(0);
    });

    it('ordering interval', async function () {
        const timers = [];
        let timeouts = 0;
        function onTimeout() {
            ++timeouts;
            for (let i = 1; i < timers.length; i++)
                clearTimeout(timers[i]);
        }
        for (let i = 0; i < 10; i++)
            timers[i] = setTimeout(onTimeout, 1);

        await waitFor(500);
        expect(timeouts).toBe(1);
    });

    it('cancel invalid silent fail', function () {
        // Should silently fail (no panic)
        clearInterval(2147483647);
    });

    it('callback this', async function () {
        const {promise, resolve} = deferred();
        const obj = {
            foo() {
                expect(this).toBe(window);
                resolve();
            },
        };
        setTimeout(obj.foo, 1);
        await promise;
    });

    it('bind this', function () {
        function noop() { }

        const thisCheckPassed = [null, undefined, window, globalThis];

        const thisCheckFailed = [
            0,
            '',
            true,
            false,
            {},
            [],
            'foo',
            () => { },
            Object.prototype,
        ];

        thisCheckPassed.forEach(thisArg => {
            expect(() => {
                setTimeout.call(thisArg, noop, 1);
            }).not.toThrow();
        });

        thisCheckFailed.forEach(thisArg => {
            expect(() => {
                setTimeout.call(thisArg, noop, 1);
            }).toThrowError(TypeError);
        });
    });

    it('function names match spec', function testFunctionName() {
        expect(clearTimeout.name).toBe('clearTimeout');
        expect(clearInterval.name).toBe('clearInterval');
    });

    it('argument lengths match spec', function testFunctionParamsLength() {
        expect(setTimeout.length).toBe(1);
        expect(setInterval.length).toBe(1);
        expect(clearTimeout.length).toBe(0);
        expect(clearInterval.length).toBe(0);
    });

    it('clear and interval are unique functions', function clearTimeoutAndClearIntervalNotBeEquals() {
        expect(clearTimeout).not.toBe(clearInterval);
    });

    // Based on https://jakearchibald.com/2015/tasks-microtasks-queues-and-schedules/
    // and https://github.com/web-platform-tests/wpt/blob/7b0ebaccc62b566a1965396e5be7bb2bc06f841f/html/webappapis/scripting/event-loops/task_microtask_ordering.html

    it('microtask ordering', async function () {
        const executionOrder = [];
        const expectedExecutionOrder = [
            'promise',
            'timeout and promise',
            'timeout',
            'callback',
        ];

        await expectPromise(resolve => {
            function execute(label) {
                executionOrder.push(label);

                if (executionOrder.length === expectedExecutionOrder.length)
                    resolve();
            }

            setTimeout(() => {
                execute('timeout');
            });

            setTimeout(() => {
                Promise.resolve().then(() => {
                    execute('timeout and promise');
                });
            });

            Promise.resolve().then(() => {
                execute('promise');
            });

            execute('callback');
        }).toBeResolved();

        expect(executionOrder).toEqual(
            jasmine.arrayWithExactContents(expectedExecutionOrder)
        );
    });

    it('nested microtask ordering', async function () {
        const executionOrder = [];
        const expectedExecutionOrder = [
            'promise 1',
            'promise 2',
            'promise 3',
            'promise 4',
            'promise 4 > nested promise',
            'promise 4 > returned promise',
            'timeout 1',
            'timeout 2',
            'timeout 3',
            'timeout 4',
            'promise 2 > nested timeout',
            'promise 3 > nested timeout',
            'promise 3 > nested timeout > nested promise',
            'timeout 1 > nested timeout',
            'timeout 2 > nested timeout',
            'timeout 2 > nested timeout > nested promise',
            'timeout 3 > nested timeout',
            'timeout 3 > nested timeout > promise',
            'timeout 3 > nested timeout > promise > nested timeout',
        ];

        await expectPromise(resolve => {
            function execute(label) {
                executionOrder.push(label);
            }

            setTimeout(() => {
                execute('timeout 1');
                setTimeout(() => {
                    execute('timeout 1 > nested timeout');
                });
            });

            setTimeout(() => {
                execute('timeout 2');
                setTimeout(() => {
                    execute('timeout 2 > nested timeout');
                    Promise.resolve().then(() => {
                        execute('timeout 2 > nested timeout > nested promise');
                    });
                });
            });

            setTimeout(() => {
                execute('timeout 3');
                setTimeout(() => {
                    execute('timeout 3 > nested timeout');
                    Promise.resolve().then(() => {
                        execute('timeout 3 > nested timeout > promise');
                        setTimeout(() => {
                            execute(
                                'timeout 3 > nested timeout > promise > nested timeout'
                            );
                            // The most deeply nested setTimeout will be the last to resolve
                            // because all queued promises should resolve prior to timeouts
                            // and timeouts execute in order
                            resolve();
                        });
                    });
                });
            });

            setTimeout(() => {
                execute('timeout 4');
            });

            Promise.resolve().then(() => {
                execute('promise 1');
            });

            Promise.resolve().then(() => {
                execute('promise 2');
                setTimeout(() => {
                    execute('promise 2 > nested timeout');
                });
            });

            Promise.resolve().then(() => {
                execute('promise 3');
                setTimeout(() => {
                    execute('promise 3 > nested timeout');

                    Promise.resolve().then(() => {
                        execute('promise 3 > nested timeout > nested promise');
                    });
                });
            });

            Promise.resolve().then(() => {
                execute('promise 4');

                Promise.resolve().then(() => {
                    execute('promise 4 > nested promise');
                });

                return Promise.resolve().then(() => {
                    execute('promise 4 > returned promise');
                });
            });
        }).toBeResolved();

        expect(executionOrder).toEqual(
            jasmine.arrayWithExactContents(expectedExecutionOrder)
        );
    });
});
