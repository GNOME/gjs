// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2018 Red Hat, Inc.

// This is where overrides in modules/core/overrides/GObject.js are tested,
// except for the class machinery, interface machinery, and GObject.ParamSpec,
// which are big enough to get their own files.

const {GLib, GObject} = imports.gi;
const {system: System} = imports;

describe('GObject overrides', function () {
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
    const types = ['gpointer', 'GBoxed', 'GParam', 'GInterface', 'GObject', 'GVariant'];

    types.forEach(type => {
        it(`be able to create a GType object for ${type}`, function () {
            const gtype = GObject.Type(type);
            expect(gtype.name).toEqual(type);
        });
    });
});
