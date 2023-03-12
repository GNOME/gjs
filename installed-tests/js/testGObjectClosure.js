// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import GObject from 'gi://GObject';

describe('GObject closure (GClosure)', function () {
    let spyFn;
    let closure;
    beforeEach(function () {
        spyFn = jasmine.createSpy();
        closure = new GObject.Closure(spyFn);
    });

    it('is an instanceof GObject.Closure', function () {
        expect(closure instanceof GObject.Closure).toBeTruthy();
    });
    
    afterEach(function () {
        closure = null;
    });
});
