// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

// Load overrides for GIMarshallingTests. This is only possible using the legacy
// importer
imports.overrides.searchPath.unshift('resource:///org/gjs/jsunit/modules/overrides');

const GIMarshallingTests = imports.gi.GIMarshallingTests;

describe('Overrides', function () {
    it('can add constants', function () {
        expect(GIMarshallingTests.OVERRIDES_CONSTANT).toEqual(7);
    });

    it('can override a struct method', function () {
        const struct = new GIMarshallingTests.OverridesStruct();
        expect(struct.method()).toEqual(6);
    });

    it('returns the overridden struct', function () {
        const obj = GIMarshallingTests.OverridesStruct.returnv();
        expect(obj).toBeInstanceOf(GIMarshallingTests.OverridesStruct);
    });

    it('can override an object constructor', function () {
        const obj = new GIMarshallingTests.OverridesObject(42);
        expect(obj.num).toEqual(42);
    });

    it('can override an object method', function () {
        const obj = new GIMarshallingTests.OverridesObject();
        expect(obj.method()).toEqual(6);
    });

    it('returns the overridden object', function () {
        const obj = GIMarshallingTests.OverridesObject.returnv();
        expect(obj).toBeInstanceOf(GIMarshallingTests.OverridesObject);
    });

    it('returns the overridden object from a C constructor', function () {
        const obj = GIMarshallingTests.OverridesObject.new();
        expect(obj).toBeInstanceOf(GIMarshallingTests.OverridesObject);
    });
});
