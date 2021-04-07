// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Lionel Landwerlin <llandwerlin@gmail.com>
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

const {GObject, Regress} = imports.gi;

const TestObj = GObject.registerClass({
    Signals: {
        'test-fundamental-value-funcs': {param_types: [Regress.TestFundamentalSubObject.$gtype]},
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
});
