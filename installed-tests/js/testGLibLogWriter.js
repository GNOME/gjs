// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line
/// <reference types="jasmine" />

const {GLib} = imports.gi;
const ByteArray = imports.byteArray;

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

    beforeEach(function () {
        writer_func.calls.reset();
    });

    it('writes a message', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_MESSAGE, {
            MESSAGE: 'a message',
        });

        const bytes = ByteArray.fromString('a message', 'UTF-8');

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            jasmine.objectContaining({MESSAGE: withElements(bytes)})
        );
    });

    it('writes a warning', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_WARNING, {
            MESSAGE: 'a warning',
        });

        const bytes = ByteArray.fromString('a warning', 'UTF-8');

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_WARNING,
            jasmine.objectContaining({MESSAGE: withElements(bytes)})
        );
    });

    it('preserves a custom string field', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_MESSAGE, {
            MESSAGE: 'with a custom field',
            GJS_CUSTOM_FIELD: 'a custom value',
        });

        const bytes = ByteArray.fromString('with a custom field', 'UTF-8');
        const custom_bytes = ByteArray.fromString('a custom value', 'UTF-8');

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            jasmine.objectContaining({
                MESSAGE: withElements(bytes),
                GJS_CUSTOM_FIELD: withElements(custom_bytes),
            })
        );
    });

    it('preserves a custom byte array field', function () {
        GLib.log_structured('Gjs-Console', GLib.LogLevelFlags.LEVEL_MESSAGE, {
            MESSAGE: 'with a custom field',
            GJS_CUSTOM_FIELD: new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7]),
        });

        const bytes = ByteArray.fromString('with a custom field', 'UTF-8');

        expect(writer_func).toHaveBeenCalledWith(
            GLib.LogLevelFlags.LEVEL_MESSAGE,
            jasmine.objectContaining({
                MESSAGE: withElements(bytes),
                GJS_CUSTOM_FIELD: withElements([0, 1, 2, 3, 4, 5, 6, 7]),
            })
        );
    });
});
