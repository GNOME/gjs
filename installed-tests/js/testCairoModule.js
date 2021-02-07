// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Philip Chimento <philip.chimento@gmail.com>

import Cairo from 'cairo';
import giCairo from 'gi://cairo';

describe('Cairo imported as ES module', function () {
    it('cairo default import', function () {
        // one from cairoNative, one from cairo JS.
        expect(typeof Cairo.Context).toBe('function');
        expect(typeof Cairo.Format).toBe('object');
    });

    // cairo doesn't have named exports
});

describe('Cairo imported via GI', function () {
    it('has the same functionality as imports.cairo', function () {
        const surface = new giCairo.ImageSurface(Cairo.Format.ARGB32, 1, 1);
        void new giCairo.Context(surface);
    });

    it('has boxed types from the GIR file', function () {
        void new giCairo.RectangleInt();
    });
});
