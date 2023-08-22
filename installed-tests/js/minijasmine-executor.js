// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import * as system from 'system';

import GLib from 'gi://GLib';
import {
    environment,
    retval,
    errorsOutput,
    mainloop,
    mainloopLock,
} from './minijasmine.js';

// environment.execute() queues up all the tests and runs them
// asynchronously. This should start after the main loop starts, otherwise
// we will hit the main loop only after several tests have already run. For
// consistency we should guarantee that there is a main loop running during
// all tests.
GLib.idle_add(GLib.PRIORITY_DEFAULT, function () {
    try {
        environment.execute();
    } catch (e) {
        print('Bail out! Exception occurred inside Jasmine:', e);

        mainloop.quit();

        system.exit(1);
    }

    return GLib.SOURCE_REMOVE;
});

// Keep running the main loop while mainloopLock is not null and resolves true.
// This happens when testing the main loop itself, in testAsyncMainloop.js. We
// don't want to exit minijasmine when the inner loop exits.
do {
    // Run the mainloop

    // This rule is to encourage parallelizing async
    // operations, in this case we don't want that.
    // eslint-disable-next-line no-await-in-loop
    await mainloop.runAsync();
// eslint-disable-next-line no-await-in-loop
} while (await mainloopLock);

// On exit, check the return value and print any errors which occurred
if (retval !== 0) {
    printerr(errorsOutput.join('\n'));
    print('# Test script failed; see test log for assertions');
    system.exit(retval);
}
