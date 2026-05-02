// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC
// SPDX-FileCopyrightText: 2026 Igalia, S.L.

import GdkPixbuf from 'gi://GdkPixbuf?version=2.0';

describe('GdkPixbuf', function () {
    describe('pixbuf', function () {
        it('can be created', function () {
            expect(GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB,
                false, 8, 1024, 1024)).toBeDefined();
        });
    });
});

