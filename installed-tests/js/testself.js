// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

describe('Test harness internal consistency', function () {
    it('', function () {
        var someUndefined;
        var someNumber = 1;
        var someOtherNumber = 42;
        var someString = 'hello';
        var someOtherString = 'world';

        expect(true).toBeTruthy();
        expect(false).toBeFalsy();

        expect(someNumber).toEqual(someNumber);
        expect(someString).toEqual(someString);

        expect(someNumber).not.toEqual(someOtherNumber);
        expect(someString).not.toEqual(someOtherString);

        expect(null).toBeNull();
        expect(someNumber).not.toBeNull();
        expect(someNumber).toBeDefined();
        expect(someUndefined).not.toBeDefined();
        expect(0 / 0).toBeNaN();
        expect(someNumber).not.toBeNaN();

        expect(() => {
            throw new Error();
        }).toThrow();

        expect(() => expect(true).toThrow()).toThrow();
        expect(() => true).not.toThrow();
    });

    describe('awaiting', function () {
        it('a Promise resolves', async function () {
            await Promise.resolve();
            expect(true).toBe(true);
        });

        async function nested() {
            await Promise.resolve();
        }

        it('a nested async function resolves', async function () {
            await nested();
            expect(true).toBe(true);
        });
    });
});

describe('SpiderMonkey features check', function () {
    it('Intl API was compiled into SpiderMonkey', function () {
        expect(Intl).toBeDefined();
    });

    it('WeakRef is enabled', function () {
        expect(WeakRef).toBeDefined();
    });

    it('class static blocks are enabled', function () {
        class Test {
            static {
                Test.x = 4;
            }
        }

        expect(Test.x).toBe(4);
    });
});
