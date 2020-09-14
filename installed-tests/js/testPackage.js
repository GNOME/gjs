// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Red Hat, Inc.

const Pkg = imports.package;

describe('Package module', function () {
    it('finds an existing library', function () {
        expect(Pkg.checkSymbol('Regress', '1.0')).toEqual(true);
    });

    it('doesn\'t find a non-existent library', function () {
        expect(Pkg.checkSymbol('Rägräss', '1.0')).toEqual(false);
    });

    it('finds a function', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'get_variant')).toEqual(true);
    });

    it('doesn\'t find a non-existent function', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'get_väriänt')).toEqual(false);
    });

    it('finds a class', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj')).toEqual(true);
    });

    it('doesn\'t find a non-existent class', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestNoObj')).toEqual(false);
    });

    it('finds a property', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj.bare')).toEqual(true);
    });

    it('doesn\'t find a non-existent property', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj.bäre')).toEqual(false);
    });

    it('finds a static function', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj.static_method')).toEqual(true);
    });

    it('doesn\'t find a non-existent static function', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj.stätic_methöd')).toEqual(false);
    });

    it('finds a method', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj.null_out')).toEqual(true);
    });

    it('doesn\'t find a non-existent method', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestObj.nüll_out')).toEqual(false);
    });

    it('finds an interface', function () {
        expect(Pkg.checkSymbol('GIMarshallingTests', '1.0', 'Interface')).toEqual(true);
    });

    it('doesn\'t find a non-existent interface', function () {
        expect(Pkg.checkSymbol('GIMarshallingTests', '1.0', 'Interfäce')).toEqual(false);
    });

    it('finds an interface method', function () {
        expect(Pkg.checkSymbol('GIMarshallingTests', '1.0', 'Interface.test_int8_in')).toEqual(true);
    });

    it('doesn\'t find a non-existent interface method', function () {
        expect(Pkg.checkSymbol('GIMarshallingTests', '1.0', 'Interface.test_int42_in')).toEqual(false);
    });

    it('finds an enum value', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestEnum.VALUE1')).toEqual(true);
    });

    it('doesn\'t find a non-existent enum value', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'TestEnum.value1')).toEqual(false);
    });

    it('finds a constant', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'BOOL_CONSTANT')).toEqual(true);
    });

    it('doesn\'t find a non-existent constant', function () {
        expect(Pkg.checkSymbol('Regress', '1.0', 'BööL_CONSTANT')).toEqual(false);
    });
});
