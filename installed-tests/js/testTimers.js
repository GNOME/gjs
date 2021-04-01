// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018-2019 the Deno authors. All rights reserved.

const {GLib} = imports.gi;

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

// eslint-disable-next-line require-await
async function waitForMs(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

describe('Timers', function () {
    it('times out successfully', async function timeoutSuccess() {
        const {promise, resolve} = deferred();
        let count = 0;
        setTimeout(() => {
            count++;
            resolve();
        }, 500);
        await promise;

        // count should increment
        expect(count).toBe(1);


        return 5;
    });

    it('has correct timeout args', async function timeoutArgs() {
        const {promise, resolve} = deferred();
        const arg = 1;

        setTimeout(
            (a, b, c) => {
                expect(a).toBe(arg);
                expect(b).toBe(arg.toString());
                expect(c).toEqual([arg]);
                resolve();
            },
            10,
            arg,
            arg.toString(),
            [arg]
        );
        await promise;
    });

    it('cancels successfully', async function timeoutCancelSuccess() {
        let count = 0;
        const id = setTimeout(() => {
            count++;
        }, 1);
        // Cancelled, count should not increment
        clearTimeout(id);
        await waitForMs(600);
        expect(count).toBe(0);
    });

    it('cancels multiple correctly', async function timeoutCancelMultiple() {
        function uncalled() {
            throw new Error('This function should not be called.');
        }

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
        await waitForMs(50);
    });

    it('cancels invalid silent fail', async function timeoutCancelInvalidSilentFail() {
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

    it('interval success', async function intervalSuccess() {
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

    it('cancels interval successfully', async function intervalCancelSuccess() {
        let count = 0;
        const id = setInterval(() => {
            count++;
        }, 1);
        clearInterval(id);
        await waitForMs(500);
        expect(count).toBe(0);
    });

    it('ordering interval', async function intervalOrdering() {
        const timers = [];
        let timeouts = 0;
        function onTimeout() {
            ++timeouts;
            for (let i = 1; i < timers.length; i++)
                clearTimeout(timers[i]);
        }
        for (let i = 0; i < 10; i++)
            timers[i] = setTimeout(onTimeout, 1);

        await waitForMs(500);
        expect(timeouts).toBe(1);
    });

    it('cancel invalid silent fail',
        // eslint-disable-next-line require-await
        async function intervalCancelInvalidSilentFail() {
            // Should silently fail (no panic)
            clearInterval(2147483647);
        });

    it('fire immediately', async function fireCallbackImmediatelyWhenDelayOverMaxValue() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*does not fit into*');

        let count = 0;
        setTimeout(() => {
            count++;
        }, 2 ** 31);
        await waitForMs(1);
        expect(count).toBe(1);
    });

    it('callback this', async function timeoutCallbackThis() {
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

    it('bind this',
        // eslint-disable-next-line require-await
        async function timeoutBindThis() {
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

            thisCheckPassed.forEach(
                thisArg => {
                    expect(() => {
                        setTimeout.call(thisArg, noop, 1);
                    }).not.toThrow();
                });

            thisCheckFailed.forEach(
                thisArg => {
                    expect(() => {
                        setTimeout.call(thisArg, noop, 1);
                    }).toThrowError(TypeError);
                }
            );
        });

    it('clearTimeout converts to number',
        // eslint-disable-next-line require-await
        async function clearTimeoutShouldConvertToNumber() {
            let called = false;
            const obj = {
                valueOf() {
                    called = true;
                    return 1;
                },
            };
            clearTimeout(obj);
            expect(called).toBe(true);
        });

    it('throw on bigint', function setTimeoutShouldThrowWithBigint() {
        expect(() => {
            setTimeout(() => { }, 1n);
        }).toThrowError(TypeError);
    });

    it('throw on bigint', function clearTimeoutShouldThrowWithBigint() {
        expect(() => {
            clearTimeout(1n);
        }).toThrowError(TypeError);
    });

    it('', function testFunctionName() {
        expect(clearTimeout.name).toBe('clearTimeout');
        expect(clearInterval.name).toBe('clearInterval');
    });

    it('length', function testFunctionParamsLength() {
        expect(setTimeout.length).toBe(1);
        expect(setInterval.length).toBe(1);
        expect(clearTimeout.length).toBe(0);
        expect(clearInterval.length).toBe(0);
    });

    it('clear and interval', function clearTimeoutAndClearIntervalNotBeEquals() {
        expect(clearTimeout).not.toBe(clearInterval);
    });

    it('microtask ordering', async function timerBasicMicrotaskOrdering() {
        let s = '';
        let count = 0;
        const {promise, resolve} = deferred();
        setTimeout(() => {
            Promise.resolve().then(() => {
                count++;
                s += 'de';
                if (count === 2)
                    resolve();
            });
        });
        setTimeout(() => {
            count++;
            s += 'no';
            if (count === 2)
                resolve();
        });
        await promise;
        expect(s).toBe('deno');
    });

    it('nested microtask ordering', async function timerNestedMicrotaskOrdering() {
        let s = '';
        const {promise, resolve} = deferred();
        s += '0';
        setTimeout(() => {
            s += '4';
            setTimeout(() => (s += '8'));
            Promise.resolve().then(() => {
                setTimeout(() => {
                    s += '9';
                    resolve();
                });
            });
        });
        setTimeout(() => (s += '5'));
        Promise.resolve().then(() => (s += '2'));
        Promise.resolve().then(() =>
            setTimeout(() => {
                s += '6';
                Promise.resolve().then(() => (s += '7'));
            })
        );
        Promise.resolve().then(() => Promise.resolve().then(() => (s += '3')));
        s += '1';
        await promise;
        expect(s).toBe('0123456789');
    });
});
