// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {acquireMainloop} from 'resource:///org/gjs/jsunit/minijasmine.js';

describe('Async mainloop', function () {
    let release;
    beforeEach(function () {
        release = acquireMainloop();
    });

    it('resolves when main loop exits', async function () {
        const loop = new GLib.MainLoop(null, false);
        const quit = jasmine.createSpy('quit').and.callFake(() => loop.quit());

        GLib.timeout_add_once(GLib.PRIORITY_DEFAULT, 50, quit);

        await loop.runAsync();

        expect(quit).toHaveBeenCalled();
    });

    it('resolves when application exits', async function () {
        const app = new Gio.Application({
            applicationId: 'org.gnome.gjs.ExampleApplication',
            flags: Gio.ApplicationFlags.HANDLES_OPEN,
        });
        app.connect('open', (theApp, [file1, file2]) => {
            expect(file1.get_basename()).toBe('foo');
            expect(file2.get_basename()).toBe('bar');
            theApp.quit();
        });

        await app.runAsync(['invocation', 'foo', 'bar']);
    });

    afterEach(function () {
        release();
    });
});
