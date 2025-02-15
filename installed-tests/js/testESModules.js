// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

import gettext from 'gettext';
import {ngettext as N_} from 'gettext';
import gi from 'gi';
import Gio from 'gi://Gio';
import system from 'system';
import {exit} from 'system';

import $ from 'resource:///org/gjs/jsunit/modules/exports.js';
import {NamedExport, data} from 'resource:///org/gjs/jsunit/modules/exports.js';
import metaProperties from 'resource:///org/gjs/jsunit/modules/importmeta.js';

// These imports should all refer to the same module and import it only once
import 'resource:///org/gjs/jsunit/modules/sideEffect.js';
import 'resource://org/gjs/jsunit/modules/sideEffect.js';
import 'resource:///org/gjs/jsunit/modules/../modules/sideEffect.js';

// Imports with query parameters should not fail and be imported uniquely
import 'resource:///org/gjs/jsunit/modules/sideEffect3.js?foo=bar&maple=syrup';
// these should resolve to the same after being canonicalized
import 'resource://org/gjs/jsunit/modules/./sideEffect3.js?etag=1';
import 'resource:///org/gjs/jsunit/modules/sideEffect3.js?etag=1';

import greeting1 from 'resource:///org/gjs/jsunit/modules/greet.js?greeting=Hello&name=Test%20Code';
import greeting2 from 'resource:///org/gjs/jsunit/modules/greet.js?greeting=Bonjour&name=Code%20de%20Test';

describe('ES module imports', function () {
    it('default import', function () {
        expect($).toEqual(5);
    });

    it('named import', function () {
        expect(NamedExport).toEqual('Hello, World');
    });

    it('GObject introspection import', function () {
        expect(gi.require('GObject').toString()).toEqual('[object GIRepositoryNamespace]');
    });

    it('import with version parameter', function () {
        expect(gi.require('GObject', '2.0')).toBe(gi.require('GObject'));
        expect(imports.gi.versions['GObject']).toBe('2.0');
    });

    it('import again with other version parameter', function () {
        expect(() => gi.require('GObject', '1.75')).toThrow();
        expect(imports.gi.versions['GObject']).toBe('2.0');
    });

    it('import for the first time with wrong version', function () {
        expect(() => gi.require('Gtk', '1.75')).toThrow();
        expect(imports.gi.versions['Gtk']).not.toBeDefined();
    });

    it('import with another version after a failed import', function () {
        expect(gi.require('Gtk', '3.0').toString()).toEqual('[object GIRepositoryNamespace]');
        expect(imports.gi.versions['Gtk']).toBe('3.0');
    });

    it('import nonexistent module', function () {
        expect(() => gi.require('PLib')).toThrow();
        expect(imports.gi.versions['PLib']).not.toBeDefined();
    });

    it('GObject introspection import via URL scheme', function () {
        expect(Gio.toString()).toEqual('[object GIRepositoryNamespace]');
        expect(imports.gi.versions['Gio']).toBe('2.0');
    });

    it('import.meta.url', function () {
        expect(import.meta.url).toMatch(/\/installed-tests\/(gjs\/)?js\/testESModules\.js$/);
    });

    it('finds files relative to import.meta.url', function () {
        // this data is loaded inside exports.js relative to import.meta.url
        expect(data).toEqual(Uint8Array.from('test data\n', c => c.codePointAt()));
    });

    it('does not expose internal import.meta properties to userland modules', function () {
        expect(metaProperties).toEqual(['url']);
    });

    it('treats equivalent URIs as equal and does not load the module again', function () {
        expect(globalThis.leakyState).toEqual(1);
    });

    it('can load modules with query parameters uniquely', function () {
        expect(globalThis.queryLeakyState).toEqual(2);
    });

    it('passes query parameters to imported modules in import.meta.uri', function () {
        expect(greeting1).toEqual('Hello, Test Code');
        expect(greeting2).toEqual('Bonjour, Code de Test');
    });

    it('rejects imports from a nonsense URI scheme', async function () {
        await expectAsync(import('resource:///org/gjs/jsunit/modules/scaryURI.js'))
            .toBeRejectedWith(jasmine.objectContaining({name: 'ImportError'}));
    });

    it('rejects imports from a real but unsupported URI scheme', async function () {
        await expectAsync(import('resource:///org/gjs/jsunit/modules/networkURI.js'))
            .toBeRejectedWith(jasmine.objectContaining({name: 'ImportError'}));
    });
});

describe('Builtin ES modules', function () {
    it('gettext default import', function () {
        expect(typeof gettext.ngettext).toBe('function');
    });

    it('gettext named import', function () {
        expect(typeof N_).toBe('function');
    });

    it('gettext named dynamic import', async function () {
        const localGettext = await import('gettext');
        expect(typeof localGettext.ngettext).toEqual('function');
    });

    it('gettext dynamic import matches static import', async function () {
        const localGettext = await import('gettext');
        expect(localGettext.default).toEqual(gettext);
    });

    it('system default import', function () {
        expect(typeof system.exit).toBe('function');
    });

    it('system named import', function () {
        expect(typeof exit).toBe('function');
        expect(exit).toBe(system.exit);
    });

    it('system dynamic import matches static import', async function () {
        const localSystem = await import('system');
        expect(localSystem.default).toEqual(system);
    });

    it('system named dynamic import', async function () {
        const localSystem = await import('system');
        expect(typeof localSystem.exit).toBe('function');
    });
});

describe('Dynamic imports', function () {
    let module;
    beforeEach(async function () {
        try {
            module = await import('resource:///org/gjs/jsunit/modules/say.js');
        } catch (err) {
            logError(err);
            fail();
        }
    });

    it('default import', function () {
        expect(module.default()).toEqual('default export');
    });

    it('named import', function () {
        expect(module.say('hi')).toEqual('<( hi )');
    });

    it('dynamic gi import matches static', async function () {
        expect((await import('gi://Gio')).default).toEqual(Gio);
    });

    it('treats equivalent URIs as equal and does not load the module again', async function () {
        delete globalThis.leakyState;
        await import('resource:///org/gjs/jsunit/modules/sideEffect2.js');
        await import('resource://org/gjs/jsunit/modules/sideEffect2.js');
        await import('resource:///org/gjs/jsunit/modules/../modules/sideEffect2.js');
        expect(globalThis.leakyState).toEqual(1);
    });

    it('treats query parameters uniquely for absolute URIs', async function () {
        delete globalThis.queryLeakyState;
        await import('resource:///org/gjs/jsunit/modules/sideEffect3.js?maple=syrup');
        expect(globalThis.queryLeakyState).toEqual(1);
    });

    it('treats query parameters uniquely for relative URIs', async function () {
        delete globalThis.queryLeakyState;
        await import('resource:///org/gjs/jsunit/modules/sideEffect4.js');
        expect(globalThis.queryLeakyState).toEqual(1);
    });

    it('does not show internal stack frames in an import error', async function () {
        try {
            await import('resource:///org/gjs/jsunit/modules/doesNotExist.js');
            fail('should not be reached');
        } catch (e) {
            expect(e.name).toBe('ImportError');
            expect(e.stack).not.toMatch('internal/');
        }
    });

    it('does not show internal stack frames in a module that throws an error', async function () {
        try {
            await import('resource:///org/gjs/jsunit/modules/alwaysThrows.js');
            fail('should not be reached');
        } catch (e) {
            expect(e.constructor).toBe(Error);
            expect(e.stack).not.toMatch('internal/');
        }
    });

    it('does not show internal stack frames in a module that fails to parse', async function () {
        try {
            // invalid JS
            await import('resource:///org/gjs/jsunit/modules/data.txt');
            fail('should not be reached');
        } catch (e) {
            expect(e.constructor).toBe(SyntaxError);
            expect(e.stack).not.toMatch('internal/');
        }
    });

    it('rejects imports from a nonsense URI scheme', async function () {
        await expectAsync(import('scary:///module.js'))
            .toBeRejectedWith(jasmine.objectContaining({name: 'ImportError'}));
    });

    it('rejects imports from a real but unsupported URI scheme', async function () {
        await expectAsync(import('https://gitlab.gnome.org/GNOME/gjs/-/raw/ce4411f5d9b6fc00ab8d949890037bd351634d5f/installed-tests/js/modules/say.js'))
            .toBeRejectedWith(jasmine.objectContaining({name: 'ImportError'}));
    });
});
