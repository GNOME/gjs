// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Lionel Landwerlin <llandwerlin@gmail.com>
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

const {GObject, Regress} = imports.gi;

const TestObj = GObject.registerClass({
    Signals: {
        'test-fundamental-value-funcs': {param_types: [Regress.TestFundamentalSubObject.$gtype]},
        'test-fundamental-no-funcs': {param_types:
            Regress.TestFundamentalObjectNoGetSetFunc
                ? [Regress.TestFundamentalObjectNoGetSetFunc.$gtype] : []},
    },
}, class TestObj extends GObject.Object {});

describe('Fundamental type support', function () {
    it('can marshal a subtype of a custom fundamental type into a GValue', function () {
        const fund = new Regress.TestFundamentalSubObject('plop');
        expect(() => GObject.strdup_value_contents(fund)).not.toThrow();

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-value-funcs', signalSpy);
        obj.emit('test-fundamental-value-funcs', fund);
        expect(signalSpy).toHaveBeenCalledWith(obj, fund);
    });

    it('can marshal a custom fundamental type into a GValue if contains a pointer and does not provide setter and getters', function () {
        const fund = new Regress.TestFundamentalObjectNoGetSetFunc('foo');
        expect(() => GObject.strdup_value_contents(fund)).not.toThrow();

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-no-funcs', signalSpy);
        obj.connect('test-fundamental-no-funcs', (_o, f) =>
            expect(f.get_data()).toBe('foo'));
        obj.emit('test-fundamental-no-funcs', fund);
        expect(signalSpy).toHaveBeenCalledWith(obj, fund);
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/merge_requests/268');

    it('cannot marshal a custom fundamental type into a GValue of different gtype', function () {
        const fund = new Regress.TestFundamentalObjectNoGetSetFunc('foo');

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-value-funcs', signalSpy);
        expect(() => obj.emit('test-fundamental-value-funcs', fund)).toThrowError(
            /.* RegressTestFundamentalObjectNoGetSetFunc .* conversion to a GValue.* RegressTestFundamentalSubObject/);
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/merge_requests/268');
});
