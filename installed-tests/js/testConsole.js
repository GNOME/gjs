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
    fields = {},
    messageMatcher = decodedStringMatching
) {
    return jasmine.objectContaining({
        MESSAGE: messageMatcher(message),
        GLIB_DOMAIN: decodedStringMatching(domain),
        ...fields,
    });
}

function matchStackTrace(log, sourceFile = null, encoding = 'utf-8') {
    const matcher = jasmine.stringMatching(log);
    const stackLineMatcher = jasmine.stringMatching(/^[\w./<]*@.*:\d+:\d+/);
    const sourceMatcher = sourceFile ? jasmine.stringMatching(RegExp(
        String.raw`^[\w]*@(file|resource):\/\/\/.*\/${sourceFile}\.js:\d+:\d+$`))
        : stackLineMatcher;

    return {
        asymmetricMatch(compareTo) {
            const decoder = new TextDecoder(encoding);
            const decoded = decoder.decode(new Uint8Array(Array.from(compareTo)));
            const lines = decoded.split('\n').filter(l => !!l.length);

            if (!matcher.asymmetricMatch(lines[0]))
                return false;

            if (!sourceMatcher.asymmetricMatch(lines[1]))
                return false;

            return lines.slice(2).every(l => stackLineMatcher.asymmetricMatch(l));
        },
        jasmineToString() {
            return `<decodedStringMatching(${log})>`;
        },
    };
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
        if (logLevel < GLib.LogLevelFlags.LEVEL_WARNING) {
            const [_, currentFile] = new Error().stack.split('\n').at(0).match(
                /^[^@]*@(.*):\d+:\d+$/);

            fields = {
                ...fields,
                CODE_FILE: decodedStringMatching(currentFile),
            };
        }

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

    afterAll(function () {
        GLib.log_set_writer_default();
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

    it('logs an empty object correctly', function () {
        const emptyObject = {};
        console.log(emptyObject);
        expectLog('{}');
    });

    it('logs an object with custom constructor name', function () {
        function CustomObject() {}
        const customInstance = new CustomObject();
        console.log(customInstance);
        expectLog('CustomObject {}');
    });

    it('logs an object with undefined constructor', function () {
        const objectWithUndefinedConstructor = Object.create(null);
        console.log(objectWithUndefinedConstructor);
        expectLog('{}');
    });

    it('logs an object with Symbol.toStringTag and __name__', function () {
        console.log(GLib);
        expectLog('[GIRepositoryNamespace GLib]');
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

    it('traces a line', function () {
        // eslint-disable-next-line max-statements-per-line
        console.trace('a trace'); const error = new Error();

        const [_, currentFile, errorLine] = error.stack.split('\n').at(0).match(
            /^[^@]*@(.*):(\d+):\d+$/);

        expect(writer_func).toHaveBeenCalledOnceWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            objectContainingLogMessage('a trace', DEFAULT_LOG_DOMAIN, {
                CODE_FILE: decodedStringMatching(currentFile),
                CODE_LINE: decodedStringMatching(errorLine),
            },
            message => matchStackTrace(message, 'testConsole'))
        );

        writer_func.calls.reset();
    });

    it('traces a empty message', function () {
        console.trace();

        const [_, currentFile] = new Error().stack.split('\n').at(0).match(
            /^[^@]*@(.*):\d+:\d+$/);

        expect(writer_func).toHaveBeenCalledOnceWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            objectContainingLogMessage('Trace', DEFAULT_LOG_DOMAIN, {
                CODE_FILE: decodedStringMatching(currentFile),
            },
            message => matchStackTrace(message, 'testConsole'))
        );

        writer_func.calls.reset();
    });

    it('asserts a true condition', function () {
        console.assert(true, 'no printed');
        expect(writer_func).not.toHaveBeenCalled();

        writer_func.calls.reset();
    });

    it('asserts a false condition', function () {
        console.assert(false);

        expectLog('Assertion failed', GLib.LogLevelFlags.LEVEL_CRITICAL);

        writer_func.calls.reset();
    });

    it('asserts a false condition with message', function () {
        console.assert(false, 'asserts false is not true');

        expectLog('asserts false is not true', GLib.LogLevelFlags.LEVEL_CRITICAL);

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
            trace: GLib.LogLevelFlags.LEVEL_MESSAGE,
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
