// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import GLib from 'gi://GLib';
import {acquireMainloop} from 'resource:///org/gjs/jsunit/minijasmine.js';

describe('Async mainloop', function () {
    let loop, quit;

    beforeEach(function () {
        loop = new GLib.MainLoop(null, false);
        quit = jasmine.createSpy('quit').and.callFake(() => {
            loop.quit();
            return GLib.SOURCE_REMOVE;
        });
    });

    it('resolves when main loop exits', async function () {
        const release = acquireMainloop();

        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, quit);

        await loop.runAsync();

        expect(quit).toHaveBeenCalled();

        release();
    });
});
