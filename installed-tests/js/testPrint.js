// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2022 Nasah Kuma <nasahnash19@gmail.com>

const GLib = imports.gi.GLib;
imports.gi.versions.Gdk = '3.0';
const Gdk = imports.gi.Gdk;

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
    afterEach(function () {
        GLib.test_assert_expected_messages_internal('Gjs', 'testPrint.js', 0,
            'pretty print');
    });

    it('property value primitive', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { greeting: "hi" }');
        log({greeting: 'hi'});
    });

    it('property value is object reference', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { a: 5, b: [Circular] }');
        let obj = {a: 5};
        obj.b = obj;
        log(obj);
    });

    it('more than one property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { a: 1, b: 2, c: 3 }');
        log({a: 1, b: 2, c: 3});
    });

    it('add property value after property value object reference', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { a: 5, b: [Circular], c: 4 }');
        let obj = {a: 5};
        obj.b = obj;
        obj.c = 4;
        log(obj);
    });

    it('array', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [1, 2, 3, 4, 5]');
        log([1, 2, 3, 4, 5]);
    });

    it('property value array', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { arr: [1, 2, 3, 4, 5] }');
        log({arr: [1, 2, 3, 4, 5]});
    });

    it('array reference is the only array element', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [[Circular]]');
        let arr = [];
        arr.push(arr);
        log(arr);
    });

    it('array reference is one of multiple array elements', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [4, [Circular], 5]');
        let arr = [];
        arr.push(4);
        arr.push(arr);
        arr.push(5);
        log(arr);
    });

    it('nested array', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [1, 2, [3, 4], 5]');
        log([1, 2, [3, 4], 5]);
    });

    it('property value nested array', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { arr: [1, 2, [3, 4], 5] }');
        log({arr: [1, 2, [3, 4], 5]});
    });

    it('function', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [ Function: sum ]');
        log(function sum(a, b) {
            return a + b;
        });
    });

    it('property value function', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { sum: [ Function: sum ] }');
        log({
            sum: function sum(a, b) {
                return a + b;
            },
        });
    });

    it('date', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: 2018-12-24T10:33:30.000Z');
        log(new Date(Date.UTC(2018, 11, 24, 10, 33, 30)));
    });

    it('property value date', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: { date: 2018-12-24T10:33:30.000Z }');
        log({date: new Date(Date.UTC(2018, 11, 24, 10, 33, 30))});
    });

    it('toString is overridden on object', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [boxed instance wrapper GIName:*]');
        log(new Gdk.Rectangle());
    });

    it('string tag supplied', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            'JS LOG: [object GIRepositoryNamespace]');
        log(Gdk);
    });
});
