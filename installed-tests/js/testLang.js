/* eslint-disable no-restricted-properties */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

// tests for imports.lang module
// except for Lang.Class and Lang.Interface, which are tested in testLegacyClass

const Lang = imports.lang;

describe('Lang module', function () {
    it('counts properties with Lang.countProperties()', function () {
        var foo = {'a': 10, 'b': 11};
        expect(Lang.countProperties(foo)).toEqual(2);
    });

    it('copies properties from one object to another with Lang.copyProperties()', function () {
        var foo = {'a': 10, 'b': 11};
        var bar = {};

        Lang.copyProperties(foo, bar);
        expect(bar).toEqual(foo);
    });

    it('copies properties without an underscore with Lang.copyPublicProperties()', function () {
        var foo = {'a': 10, 'b': 11, '_c': 12};
        var bar = {};

        Lang.copyPublicProperties(foo, bar);
        expect(bar).toEqual({'a': 10, 'b': 11});
    });

    it('copies property getters and setters', function () {
        var foo = {
            'a': 10,
            'b': 11,
            get c() {
                return this.a;
            },
            set c(n) {
                this.a = n;
            },
        };
        var bar = {};

        Lang.copyProperties(foo, bar);

        expect(bar.__lookupGetter__('c')).not.toBeNull();
        expect(bar.__lookupSetter__('c')).not.toBeNull();

        // this should return the value of 'a'
        expect(bar.c).toEqual(10);

        // this should set 'a' value
        bar.c = 13;
        expect(bar.a).toEqual(13);
    });


    describe('bind()', function () {
        let o;

        beforeEach(function () {
            o = {
                callback() {
                    return true;
                },
            };
            spyOn(o, 'callback').and.callThrough();
        });

        it('calls the bound function with the supplied this-object', function () {
            let callback = Lang.bind(o, o.callback);
            callback();
            expect(o.callback.calls.mostRecent()).toEqual(jasmine.objectContaining({
                object: o,
                args: [],
                returnValue: true,
            }));
        });

        it('throws an error when no function supplied', function () {
            expect(() => Lang.bind(o, undefined)).toThrow();
        });

        it('throws an error when this-object undefined', function () {
            expect(() => Lang.bind(undefined, function () {})).toThrow();
        });

        it('supplies extra arguments to the function', function () {
            let callback = Lang.bind(o, o.callback, 42, 1138);
            callback();
            expect(o.callback).toHaveBeenCalledWith(42, 1138);
        });

        it('appends the extra arguments to any arguments passed', function () {
            let callback = Lang.bind(o, o.callback, 42, 1138);
            callback(1, 2, 3);
            expect(o.callback).toHaveBeenCalledWith(1, 2, 3, 42, 1138);
        });
    });
});
