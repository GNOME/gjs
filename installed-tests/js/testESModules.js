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
});
