// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2022 Nasah Kuma <nasahnash19@gmail.com>

imports.gi.versions.Gdk = '3.0';
const Gdk = imports.gi.Gdk;
const {getPrettyPrintFunction} = imports._print;
let prettyPrint = getPrettyPrintFunction(globalThis);

describe('print', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'print');
        print('foo');
        expect(print).toHaveBeenCalledWith('foo');
    });
});

describe('printerr', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'printerr');
        printerr('foo');
        expect(printerr).toHaveBeenCalledWith('foo');
    });
});

describe('log', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'log');
        log('foo');
        expect(log).toHaveBeenCalledWith('foo');
    });
});

describe('logError', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'logError');
        logError('foo', 'bar');
        expect(logError).toHaveBeenCalledWith('foo', 'bar');
    });
});

describe('prettyPrint', function () {
    it('property value primitive', function () {
        expect(
            prettyPrint({greeting: 'hi'})
        ).toBe('{ greeting: "hi" }');
    });

    it('property value is object reference', function () {
        let obj = {a: 5};
        obj.b = obj;
        expect(
            prettyPrint(obj)
        ).toBe('{ a: 5, b: [Circular] }');
    });

    it('more than one property', function () {
        expect(
            prettyPrint({a: 1, b: 2, c: 3})
        ).toBe('{ a: 1, b: 2, c: 3 }');
    });

    it('add property value after property value object reference', function () {
        let obj = {a: 5};
        obj.b = obj;
        obj.c = 4;
        expect(
            prettyPrint(obj)
        ).toBe('{ a: 5, b: [Circular], c: 4 }');
    });

    it('array', function () {
        expect(
            prettyPrint([1, 2, 3, 4, 5])
        ).toBe('[1, 2, 3, 4, 5]');
    });

    it('property value array', function () {
        expect(
            prettyPrint({arr: [1, 2, 3, 4, 5]})
        ).toBe('{ arr: [1, 2, 3, 4, 5] }');
    });

    it('array reference is the only array element', function () {
        let arr = [];
        arr.push(arr);
        expect(
            prettyPrint(arr)
        ).toBe('[[Circular]]');
    });

    it('array reference is one of multiple array elements', function () {
        let arr = [];
        arr.push(4);
        arr.push(arr);
        arr.push(5);
        expect(
            prettyPrint(arr)
        ).toBe('[4, [Circular], 5]');
    });

    it('nested array', function () {
        expect(
            prettyPrint([1, 2, [3, 4], 5])
        ).toBe('[1, 2, [3, 4], 5]');
    });

    it('property value nested array', function () {
        expect(
            prettyPrint({arr: [1, 2, [3, 4], 5]})
        ).toBe('{ arr: [1, 2, [3, 4], 5] }');
    });

    it('function', function () {
        expect(
            prettyPrint(function sum(a, b) {
                return a + b;
            })
        ).toBe('[ Function: sum ]');
    });

    it('property value function', function () {
        expect(
            prettyPrint({
                sum: function sum(a, b) {
                    return a + b;
                },
            })
        ).toBe('{ sum: [ Function: sum ] }');
    });

    it('date', function () {
        expect(
            prettyPrint(new Date(Date.UTC(2018, 11, 24, 10, 33, 30)))
        ).toBe('2018-12-24T10:33:30.000Z');
    });

    it('property value date', function () {
        expect(
            prettyPrint({date: new Date(Date.UTC(2018, 11, 24, 10, 33, 30))})
        ).toBe('{ date: 2018-12-24T10:33:30.000Z }');
    });

    it('toString is overridden on object', function () {
        expect(
            prettyPrint(new Gdk.Rectangle())
        ).toMatch(/\[boxed instance wrapper GIName:.*\]/);
    });

    it('string tag supplied', function () {
        expect(
            prettyPrint(Gdk)
        ).toMatch('[object GIRepositoryNamespace]');
    });
});
