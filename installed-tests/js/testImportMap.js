// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

describe('Import map', function () {
    it('does not apply to internal imports', function () {
        // The import map tries to hijack the internal 'console' import
        expect(globalThis.console).toBeDefined();
    });

    it('is sorted correctly', async function () {
        // The "sorttest/sub/" rule should be sorted before the "sorttest/" rule
        const module = await import('sorttest/sub/x.js');
        expect(module.default).toBe(42);
    });

    it('applies file scopes', async function () {
        const module = await import('resource:///org/gjs/jsunit/modules/importMapFileScope.js');
        expect(module.default).toBe(42);
    });

    it('remaps imports starting with /', async function () {
        const module = await import('resource:///org/gjs/jsunit/modules/importMapSlash.js');
        expect(module.default).toBe(42);
    });

    it('applies scopes to a URL that was already imported unscoped', async function () {
        // This URL is imported from outside the scope and must not be remapped
        const unscoped = await import('resource:///org/gjs/jsunit/modules/sub/x.js');
        expect(unscoped.default).toBe(77);

        // importer.js is inside the scope and imports the same URL which must
        // be remapped
        const importer = await import('resource:///org/gjs/jsunit/modules/loadorder/importer.js');
        expect(importer.default).toBe(42);
        const dynamicImport = await importer.dynamicImport('resource:///org/gjs/jsunit/modules/sub/x.js');
        expect(dynamicImport.default).toBe(42);
    });

    it('applies the most specific matching scope', async function () {
        // Both the modules/ and modules/loadorder/ scopes map this URL
        const subX = 'resource:///org/gjs/jsunit/modules/sub/x.js';
        const outer = await import('resource:///org/gjs/jsunit/modules/importer.js');
        expect((await outer.dynamicImport(subX)).default).toBe(5);
        const inner = await import('resource:///org/gjs/jsunit/modules/loadorder/importer.js');
        expect((await inner.dynamicImport(subX)).default).toBe(42);
    });

    it('falls back to the top-level imports if no scope maps a specifier', async function () {
        // "sorttest/sub/" is only mapped in the top-level imports
        const importer = await import('resource:///org/gjs/jsunit/modules/loadorder/importer.js');
        const module = await importer.dynamicImport('sorttest/sub/x.js');
        expect(module.default).toBe(42);
    });

    it('can remap a built-in module', async function () {
        const module = await import('cairo');
        expect(module.default).toBe(42);
    });

    it('remaps a relative import by its resolved URL', async function () {
        // "./relativetest.js" resolves to a URL that is mapped, but doesn't exist
        const importer = await import('resource:///org/gjs/jsunit/modules/importer.js');
        const module = await importer.dynamicImport('./relativetest.js');
        expect(module.default).toBe(42);
    });

    it('throws ImportError for an unknown bare specifier', async function () {
        await expectAsync(import('nonexistentimportmaptest'))
            .toBeRejectedWith(jasmine.objectContaining({name: 'ImportError'}));
    });

    it('rejects a prefix mapping that backtracks with ../', async function () {
        // "dotdottest/" maps to othersub/, and "../sub/x.js" would resolve to a
        // module outside of that directory
        await expectAsync(import('dotdottest/../sub/x.js'))
            .toBeRejectedWithError(TypeError, /backtracked/);
    });

    describe('with query strings', function () {
        it('applies an import map entry with a query to the URL with the same query', async function () {
            const module = await import('resource:///org/gjs/jsunit/modules/meta.js?v=1');
            expect(module.default).toBe(42);
        });

        it('does not apply an import map entry with a query to the same URL but a different query', async function () {
            const module = await import('resource:///org/gjs/jsunit/modules/meta.js?v=2');
            expect(module.default).toBe('resource:///org/gjs/jsunit/modules/meta.js?v=2');
        });

        it('does not apply an import map entry with a query to the same URL with no query', async function () {
            const module = await import('resource:///org/gjs/jsunit/modules/meta.js');
            expect(module.default).toBe('resource:///org/gjs/jsunit/modules/meta.js');
        });

        it('keeps the query when remapping a prefix', async function () {
            const module = await import('resource:///org/gjs/jsunit/modules/queryprefix/meta.js?q=1');
            expect(module.default).toBe('resource:///org/gjs/jsunit/modules/meta.js?q=1');
        });

        it('rejects a prefix mapping to a URL with a query string', async function () {
            // "backtracktest/" maps to a URL that ends in a slash only because
            // of its query string. Resolving "sub/x.js" against that URL drops
            // the query, so the result no longer starts with the mapped URL,
            // which fails the spec's 'backtracking' rule.
            await expectAsync(import('backtracktest/sub/x.js'))
                .toBeRejectedWithError(TypeError, /backtracked/);
        });

        it('does not apply a gi:// key with a version to no version', async function () {
            const {default: Gio} = await import('gi://Gio');
            expect(Gio.__name__).toBe('Gio');
        });
    });
});
