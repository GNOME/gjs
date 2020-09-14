// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Lionel Landwerlin <llandwerlin@gmail.com>

const {GObject, Regress} = imports.gi;

describe('Fundamental type support', function () {
    it('can marshal a subtype of a custom fundamental type into a GValue', function () {
        const fund = new Regress.TestFundamentalSubObject('plop');
        expect(() => GObject.strdup_value_contents(fund)).not.toThrow();
    });
});
