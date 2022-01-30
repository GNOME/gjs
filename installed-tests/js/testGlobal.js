// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

describe('globalThis', () => {
    function itIsDefined(value, message) {
        it(`${message ? `${message} ` : ''}is defined`, function () {
            expect(value).toBeDefined();
        });
    }

    it('is equal to window', function () {
        expect(globalThis.window).toBe(globalThis);
        expect(window.globalThis).toBe(globalThis);
    });

    describe('WeakRef', () => {
        itIsDefined(globalThis.WeakRef);
    });

    describe('console', () => {
        itIsDefined(globalThis.console);
    });

    describe('TextEncoder', () => {
        itIsDefined(globalThis.TextEncoder);
    });

    describe('TextDecoder', () => {
        itIsDefined(globalThis.TextDecoder);
    });

    describe('ARGV', () => {
        itIsDefined(globalThis.ARGV);
    });

    describe('print function', () => {
        itIsDefined(globalThis.log, 'log');
        itIsDefined(globalThis.print, 'print');
        itIsDefined(globalThis.printerr, 'printerr');
        itIsDefined(globalThis.logError, 'logError');
    });
});
