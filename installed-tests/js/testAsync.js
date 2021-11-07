// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

import GLib from 'gi://GLib';
import GObject from 'gi://GObject';

const PRIORITIES = [
    'PRIORITY_LOW',
    'PRIORITY_HIGH',
    'PRIORITY_DEFAULT',
    'PRIORITY_HIGH_IDLE',
    'PRIORITY_DEFAULT_IDLE',
];

describe('Async microtasks resolves before', function () {
    // Generate test suites with different types of Sources
    const tests = [
        {
            description: 'idle task with',
            createSource: () => GLib.idle_source_new(),
        },
        {
            description: '0-second timeout task with',
            // A timeout of 0 tests if microtasks (promises) run before
            // non-idle tasks which would normally execute "next" in the loop
            createSource: () => GLib.timeout_source_new(0),
        },
    ];

    for (const {description, createSource} of tests) {
        describe(description, function () {
            const CORRECT_TASKS = [
                'async 1',
                'async 2',
                'source task',
            ];

            for (const priority of PRIORITIES) {
                it(`priority set to GLib.${priority}`, function (done) {
                    const tasks = [];

                    const source = createSource();
                    source.set_priority(GLib[priority]);
                    GObject.source_set_closure(source, () => {
                        tasks.push('source task');

                        expect(tasks).toEqual(jasmine.arrayWithExactContents(CORRECT_TASKS));

                        done();
                        source.destroy();

                        return GLib.SOURCE_REMOVE;
                    });
                    source.attach(null);

                    (async () => {
                        // Without an await async functions execute
                        // synchronously
                        tasks.push(await 'async 1');
                    })().then(() => {
                        tasks.push('async 2');
                    });
                });
            }
        });
    }
});
