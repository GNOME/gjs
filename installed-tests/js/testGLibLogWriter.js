// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line
/// <reference types="jasmine" />

import GLib from 'gi://GLib';
import {arrayLikeWithExactContents} from './matchers.js';

function encodedString(str) {
    const encoder = new TextEncoder();
    const encoded = encoder.encode(str);

    return arrayLikeWithExactContents(encoded);
}

describe('GLib Structured logging handler', function () {
    /** @type {jasmine.Spy<(_level: any, _fields: any) => any>} */
    let writer_func;

    beforeAll(function () {
        writer_func = jasmine.createSpy(
            'Log test writer func',
            function (_level, _fields) {
                return GLib.LogWriterOutput.HANDLED;
            }
        );

        writer_func.and.callThrough();

        GLib.log_set_writer_func(writer_func);
    });

    afterAll(function () {
        GLib.log_set_writer_default();
    });

    beforeEach(function () {
        writer_func.calls.reset();
    });

    it('writes a message', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_MESSAGE, {
            MESSAGE: 'a message',
        });

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            jasmine.objectContaining({MESSAGE: encodedString('a message')})
        );
    });

    it('writes a warning', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_WARNING, {
            MESSAGE: 'a warning',
        });

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_WARNING,
            jasmine.objectContaining({MESSAGE: encodedString('a warning')})
        );
    });

    it('preserves a custom string field', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_MESSAGE, {
            MESSAGE: 'with a custom field',
            GJS_CUSTOM_FIELD: 'a custom value',
        });

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            jasmine.objectContaining({
                MESSAGE: encodedString('with a custom field'),
                GJS_CUSTOM_FIELD: encodedString('a custom value'),
            })
        );
    });

    it('preserves a custom byte array field', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_MESSAGE, {
            MESSAGE: 'with a custom field',
            GJS_CUSTOM_FIELD: new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7]),
        });

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            jasmine.objectContaining({
                MESSAGE: encodedString('with a custom field'),
                GJS_CUSTOM_FIELD: arrayLikeWithExactContents([
                    0, 1, 2, 3, 4, 5, 6, 7,
                ]),
            })
        );
    });
});
