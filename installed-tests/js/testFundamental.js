// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Lionel Landwerlin <llandwerlin@gmail.com>
// SPDX-FileCopyrightText: 2021 Marco Trevisan <marco.trevisan@canonical.com>

import GObject from 'gi://GObject';
import Regress from 'gi://Regress';

const TestObj = GObject.registerClass({
    Signals: {
        'test-fundamental-value-funcs': {param_types: [Regress.TestFundamentalObject.$gtype]},
        'test-fundamental-value-funcs-subtype': {param_types: [Regress.TestFundamentalSubObject.$gtype]},
        'test-fundamental-no-funcs': {
            param_types: Regress.TestFundamentalObjectNoGetSetFunc
                ? [Regress.TestFundamentalObjectNoGetSetFunc.$gtype] : [],
        },
        'test-fundamental-no-funcs-subtype': {
            param_types: Regress.TestFundamentalSubObjectNoGetSetFunc
                ? [Regress.TestFundamentalSubObjectNoGetSetFunc.$gtype] : [],
        },
    },
}, class TestObj extends GObject.Object {});

describe('Fundamental type support', function () {
    it('can marshal a subtype of a custom fundamental type into a supertype GValue', function () {
        const fund = new Regress.TestFundamentalSubObject('plop');
        expect(() => GObject.strdup_value_contents(fund)).not.toThrow();

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-value-funcs', signalSpy);
        obj.emit('test-fundamental-value-funcs', fund);
        expect(signalSpy).toHaveBeenCalledWith(obj, fund);
    });

    it('can marshal a subtype of a custom fundamental type into a GValue', function () {
        const fund = new Regress.TestFundamentalSubObject('plop');
        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-value-funcs-subtype', signalSpy);
        obj.emit('test-fundamental-value-funcs-subtype', fund);
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
    });

    it('can marshal a subtype of a custom fundamental type into a GValue if contains a pointer and does not provide setter and getters', function () {
        const fund = new Regress.TestFundamentalSubObjectNoGetSetFunc('foo');

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-no-funcs-subtype', signalSpy);
        obj.connect('test-fundamental-no-funcs-subtype', (_o, f) =>
            expect(f.get_data()).toBe('foo'));
        obj.emit('test-fundamental-no-funcs-subtype', fund);
        expect(signalSpy).toHaveBeenCalledWith(obj, fund);
    });

    it('cannot marshal a custom fundamental type into a GValue of different gtype', function () {
        const fund = new Regress.TestFundamentalObjectNoGetSetFunc('foo');

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-value-funcs', signalSpy);
        expect(() => obj.emit('test-fundamental-value-funcs', fund)).toThrowError(
            / RegressTestFundamentalObjectNoGetSetFunc .* conversion to a GValue.* RegressTestFundamentalObject/);
    });

    it('can marshal a custom fundamental type into a GValue of super gtype', function () {
        const fund = new Regress.TestFundamentalSubObjectNoGetSetFunc('foo');

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-no-funcs', signalSpy);
        obj.connect('test-fundamental-no-funcs', (_o, f) =>
            expect(f.get_data()).toBe('foo'));
        obj.emit('test-fundamental-no-funcs', fund);
        expect(signalSpy).toHaveBeenCalledWith(obj, fund);
    });

    it('cannot marshal a custom fundamental type into a GValue of sub gtype', function () {
        const fund = new Regress.TestFundamentalObjectNoGetSetFunc('foo');

        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-no-funcs-subtype', signalSpy);
        expect(() => obj.emit('test-fundamental-no-funcs-subtype', fund)).toThrowError(
            / RegressTestFundamentalObjectNoGetSetFunc .* conversion to a GValue.* RegressTestFundamentalSubObjectNoGetSetFunc/);
    });

    it('can marshal a custom fundamental type into a transformable type', function () {
        Regress.TestFundamentalObjectNoGetSetFunc.make_compatible_with_fundamental_sub_object();
        const fund = new Regress.TestFundamentalObjectNoGetSetFunc('foo');
        const obj = new TestObj();
        const signalSpy = jasmine.createSpy('signalSpy');
        obj.connect('test-fundamental-value-funcs-subtype', signalSpy);
        obj.connect('test-fundamental-value-funcs-subtype', (_o, f) =>
            expect(f instanceof Regress.TestFundamentalSubObject).toBeTrue());
        obj.emit('test-fundamental-value-funcs-subtype', fund);
        expect(signalSpy).toHaveBeenCalled();
    });

    it('can marshal to a null value', function () {
        const v = new GObject.Value();
        expect(v.init(Regress.TestFundamentalObject.$gtype)).toBeNull();
    });

    it('can marshal to a null value if has no getter function', function () {
        const v = new GObject.Value();
        expect(v.init(Regress.TestFundamentalObjectNoGetSetFunc.$gtype)).toBeNull();
    });
});
