// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Philip Chimento <philip.chimento@gmail.com>

// This test is in a separate file instead of testImporter.js, because it tests
// loading overrides for g-i modules, and in the original file we have literally
// run out of g-i modules to override -- at least, the ones that we can assume
// will be present on any system where GJS is compiled.

describe('GI importer', function () {
    describe('on failure', function () {
        // For these tests, we provide special overrides files to sabotage the
        // import, at the path resource:///org/gjs/jsunit/modules/badOverrides2.
        let oldSearchPath;
        beforeAll(function () {
            oldSearchPath = imports.overrides.searchPath.slice();
            imports.overrides.searchPath = ['resource:///org/gjs/jsunit/modules/badOverrides2'];
        });

        afterAll(function () {
            imports.overrides.searchPath = oldSearchPath;
        });

        it("throws an exception when the overrides _init isn't a function", function () {
            expect(() => imports.gi.GIMarshallingTests).toThrowError(/_init/);
        });

        it('throws an exception when the overrides _init is null', function () {
            expect(() => imports.gi.Gio).toThrowError(/_init/);
        });

        it('throws an exception when the overrides _init is undefined', function () {
            expect(() => imports.gi.Regress).toThrowError(/_init/);
        });

        it('throws an exception when the overrides _init is missing', function () {
            expect(() => imports.gi.WarnLib).toThrowError(/_init/);
        });
    });
});
