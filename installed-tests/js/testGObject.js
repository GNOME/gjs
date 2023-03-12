// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2018 Red Hat, Inc.

// This is where overrides in modules/core/overrides/GObject.js are tested,
// except for the class machinery, interface machinery, and GObject.ParamSpec,
// which are big enough to get their own files.

const {GLib, GObject} = imports.gi;
const {system: System} = imports;

const TestObj = GObject.registerClass({
    Properties: {
        int: GObject.ParamSpec.int('int', '', '', GObject.ParamFlags.READWRITE,
            0, GLib.MAXINT32, 0),
        string: GObject.ParamSpec.string('string', '', '',
            GObject.ParamFlags.READWRITE, ''),
    },
    Signals: {
        test: {},
    },
}, class TestObj extends GObject.Object {});

describe('GObject overrides', function () {
    it('GObject.set()', function () {
        const o = new TestObj();
        o.set({string: 'Answer', int: 42});
        expect(o.string).toBe('Answer');
        expect(o.int).toBe(42);
    });

    describe('Signal alternative syntax', function () {
        let o, handler;
        beforeEach(function () {
            handler = jasmine.createSpy('handler');
            o = new TestObj();
            const handlerId = GObject.signal_connect(o, 'test', handler);
            handler.and.callFake(() =>
                GObject.signal_handler_disconnect(o, handlerId));

            GObject.signal_emit_by_name(o, 'test');
        });

        it('handler is called with the right object', function () {
            expect(handler).toHaveBeenCalledTimes(1);
            expect(handler).toHaveBeenCalledWith(o);
        });

        it('disconnected handler is not called', function () {
            handler.calls.reset();
            GObject.signal_emit_by_name(o, 'test');
            expect(handler).not.toHaveBeenCalled();
        });
    });

    it('toString() shows the native object address', function () {
        const o = new TestObj();
        const address = System.addressOfGObject(o);
        expect(o.toString()).toMatch(
            new RegExp(`[object instance wrapper .* jsobj@0x[a-f0-9]+ native@${address}`));
    });
});

describe('GObject should', function () {
    const types = ['gpointer', 'GBoxed', 'GParam', 'GInterface', 'GObject', 'GVariant', 'GClosure'];

    types.forEach(type => {
        it(`be able to create a GType object for ${type}`, function () {
            const gtype = GObject.Type(type);
            expect(gtype.name).toEqual(type);
        });
    });

    it('be able to query signals', function () {
        const query = GObject.signal_query(1);

        expect(query instanceof GObject.SignalQuery).toBeTruthy();
        expect(query.param_types).not.toBeNull();
        expect(Array.isArray(query.param_types)).toBeTruthy();
        expect(query.signal_id).toBe(1);
    });
});

describe('GObject.Object.new()', function () {
    const gon = GObject.Object.new;

    it('can be called with a property bag', function () {
        const o = gon(TestObj, {
            string: 'Answer',
            int: 42,
        });
        expect(o.string).toBe('Answer');
        expect(o.int).toBe(42);
    });

    it('can be called to construct an object without setting properties', function () {
        const o1 = gon(TestObj);
        expect(o1.string).toBe('');
        expect(o1.int).toBe(0);

        const o2 = gon(TestObj, {});
        expect(o2.string).toBe('');
        expect(o2.int).toBe(0);
    });

    it('complains about wrong types', function () {
        expect(() => gon(TestObj, {
            string: 42,
            int: 'Answer',
        })).toThrow();
    });

    it('complains about wrong properties', function () {
        expect(() => gon(TestObj, {foo: 'bar'})).toThrow();
    });

    it('can construct C GObjects as well', function () {
        const o = gon(GObject.Object, {});
        expect(o.constructor.$gtype.name).toBe('GObject');
    });
});

describe('GObject.Object.new_with_properties()', function () {
    const gonwp = GObject.Object.new_with_properties;

    it('can be called with two arrays', function () {
        const o = gonwp(TestObj, ['string', 'int'], ['Answer', 42]);
        expect(o.string).toBe('Answer');
        expect(o.int).toBe(42);
    });

    it('can be called to construct an object without setting properties', function () {
        const o = gonwp(TestObj, [], []);
        expect(o.string).toBe('');
        expect(o.int).toBe(0);
    });

    it('complains about various incorrect usages', function () {
        expect(() => gonwp(TestObj)).toThrow();
        expect(() => gonwp(TestObj, ['string', 'int'])).toThrow();
        expect(() => gonwp(TestObj, ['string', 'int'], ['Answer'])).toThrow();
        expect(() => gonwp(TestObj, {}, ['Answer', 42])).toThrow();
    });

    it('complains about wrong types', function () {
        expect(() => gonwp(TestObj, ['string', 'int'], [42, 'Answer'])).toThrow();
    });

    it('complains about wrong properties', function () {
        expect(() => gonwp(TestObj, ['foo'], ['bar'])).toThrow();
    });

    it('can construct C GObjects as well', function () {
        const o = gonwp(GObject.Object, [], []);
        expect(o.constructor.$gtype.name).toBe('GObject');
    });
});
