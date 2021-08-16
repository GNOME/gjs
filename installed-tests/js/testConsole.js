// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line
/// <reference types="jasmine" />

import GLib from 'gi://GLib';
import {DEFAULT_LOG_DOMAIN} from 'console';

import {decodedStringMatching} from './matchers.js';

function objectContainingLogMessage(
    message,
    domain = DEFAULT_LOG_DOMAIN,
    fields = {}
) {
    return jasmine.objectContaining({
        MESSAGE: decodedStringMatching(message),
        GLIB_DOMAIN: decodedStringMatching(domain),
        ...fields,
    });
}

describe('console', function () {
    /** @type {jasmine.Spy<(_level: any, _fields: any) => any>} */
    let writer_func;

    /**
     * @param {RegExp | string} message _
     * @param {*} [logLevel] _
     * @param {*} [domain] _
     * @param {*} [fields] _
     */
    function expectLog(
        message,
        logLevel = GLib.LogLevelFlags.LEVEL_MESSAGE,
        domain = DEFAULT_LOG_DOMAIN,
        fields = {}
    ) {
        expect(writer_func).toHaveBeenCalledOnceWith(
            logLevel,
            objectContainingLogMessage(message, domain, fields)
        );

        // Always reset the calls, so that we can assert at the end that no
        // unexpected messages were logged
        writer_func.calls.reset();
    }

    beforeAll(function () {
        writer_func = jasmine.createSpy(
            'Console test writer func',
            function (level, _fields) {
                if (level === GLib.LogLevelFlags.ERROR)
                    return GLib.LogWriterOutput.UNHANDLED;

                return GLib.LogWriterOutput.HANDLED;
            }
        );

        writer_func.and.callThrough();

        // @ts-expect-error The existing binding doesn't accept any parameters because
        // it is a raw pointer.
        GLib.log_set_writer_func(writer_func);
    });

    beforeEach(function () {
        writer_func.calls.reset();
    });

    it('has correct object tag', function () {
        expect(console.toString()).toBe('[object console]');
    });

    it('logs a message', function () {
        console.log('a log');

        expect(writer_func).toHaveBeenCalledOnceWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            objectContainingLogMessage('a log')
        );
        writer_func.calls.reset();
    });

    it('logs a warning', function () {
        console.warn('a warning');

        expect(writer_func).toHaveBeenCalledOnceWith(
            GLib.LogLevelFlags.LEVEL_WARNING,
            objectContainingLogMessage('a warning')
        );
        writer_func.calls.reset();
    });

    it('logs an informative message', function () {
        console.info('an informative message');

        expect(writer_func).toHaveBeenCalledOnceWith(
            GLib.LogLevelFlags.LEVEL_INFO,
            objectContainingLogMessage('an informative message')
        );
        writer_func.calls.reset();
    });

    describe('clear()', function () {
        it('can be called', function () {
            console.clear();
        });

        it('resets indentation', function () {
            console.group('a group');
            expectLog('a group');
            console.log('a log');
            expectLog('  a log');
            console.clear();
            console.log('a log');
            expectLog('a log');
        });
    });

    describe('table()', function () {
        it('logs at least something', function () {
            console.table(['title', 1, 2, 3]);
            expectLog(/title/);
        });
    });

    // %s - string
    // %d or %i - integer
    // %f - float
    // %o  - "optimal" object formatting
    // %O - "generic" object formatting
    // %c - "CSS" formatting (unimplemented by GJS)
    describe('string replacement', function () {
        const functions = {
            log: GLib.LogLevelFlags.LEVEL_MESSAGE,
            warn: GLib.LogLevelFlags.LEVEL_WARNING,
            info: GLib.LogLevelFlags.LEVEL_INFO,
            error: GLib.LogLevelFlags.LEVEL_CRITICAL,
        };

        Object.entries(functions).forEach(([fn, level]) => {
            it(`console.${fn}() supports %s`, function () {
                console[fn]('Does this %s substitute correctly?', 'modifier');
                expectLog('Does this modifier substitute correctly?', level);
            });

            it(`console.${fn}() supports %d`, function () {
                console[fn]('Does this %d substitute correctly?', 10);
                expectLog('Does this 10 substitute correctly?', level);
            });

            it(`console.${fn}() supports %i`, function () {
                console[fn]('Does this %i substitute correctly?', 26);
                expectLog('Does this 26 substitute correctly?', level);
            });

            it(`console.${fn}() supports %f`, function () {
                console[fn]('Does this %f substitute correctly?', 27.56331);
                expectLog('Does this 27.56331 substitute correctly?', level);
            });

            it(`console.${fn}() supports %o`, function () {
                console[fn]('Does this %o substitute correctly?', new Error());
                expectLog(/Does this Error\n.*substitute correctly\?/s, level);
            });

            it(`console.${fn}() supports %O`, function () {
                console[fn]('Does this %O substitute correctly?', new Error());
                expectLog('Does this {} substitute correctly?', level);
            });

            it(`console.${fn}() ignores %c`, function () {
                console[fn]('Does this %c substitute correctly?', 'modifier');
                expectLog('Does this  substitute correctly?', level);
            });

            it(`console.${fn}() supports mixing substitutions`, function () {
                console[fn](
                    'Does this %s and the %f substitute correctly alongside %d?',
                    'string',
                    3.14,
                    14
                );
                expectLog(
                    'Does this string and the 3.14 substitute correctly alongside 14?',
                    level
                );
            });

            it(`console.${fn}() supports invalid numbers`, function () {
                console[fn](
                    'Does this support parsing %i incorrectly?',
                    'a string'
                );
                expectLog('Does this support parsing NaN incorrectly?', level);
            });

            it(`console.${fn}() supports missing substitutions`, function () {
                console[fn]('Does this support a missing %s substitution?');
                expectLog(
                    'Does this support a missing %s substitution?',
                    level
                );
            });
        });
    });

    describe('time()', function () {
        it('ends correctly', function (done) {
            console.time('testing time');

            // console.time logs nothing.
            expect(writer_func).not.toHaveBeenCalled();

            setTimeout(() => {
                console.timeLog('testing time');

                expectLog(/testing time: (.*)ms/);

                console.timeEnd('testing time');

                expectLog(/testing time: (.*)ms/);

                console.timeLog('testing time');

                expectLog(
                    "No time log found for label: 'testing time'.",
                    GLib.LogLevelFlags.LEVEL_WARNING
                );

                done();
            }, 10);
        });

        it("doesn't log initially", function (done) {
            console.time('testing time');

            // console.time logs nothing.
            expect(writer_func).not.toHaveBeenCalled();

            setTimeout(() => {
                console.timeEnd('testing time');
                expectLog(/testing time: (.*)ms/);

                done();
            }, 10);
        });

        afterEach(function () {
            // Ensure we only got the log lines that we expected
            expect(writer_func).not.toHaveBeenCalled();
        });
    });
});
