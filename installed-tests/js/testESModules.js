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
    });

    it('import again with other version parameter', function () {
        expect(() => gi.require('GObject', '1.75')).toThrow();
    });

    it('import for the first time with wrong version', function () {
        expect(() => gi.require('Gtk', '1.75')).toThrow();
    });

    it('import with another version after a failed import', function () {
        expect(gi.require('Gtk', '3.0').toString()).toEqual('[object GIRepositoryNamespace]');
    });

    it('import nonexistent module', function () {
        expect(() => gi.require('PLib')).toThrow();
    });

    it('GObject introspection import via URL scheme', function () {
        expect(Gio.toString()).toEqual('[object GIRepositoryNamespace]');
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
});
