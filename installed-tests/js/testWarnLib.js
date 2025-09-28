// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.
// SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

// File with tests from the WarnLib-1.0.gir test suite from GI

import Gio from 'gi://Gio';
import GObject from 'gi://GObject';
import WarnLib from 'gi://WarnLib';

describe('WarnLib', function () {
    // Calling matches() on an unpaired error used to JSUnit.assert:
    // https://bugzilla.gnome.org/show_bug.cgi?id=689482
    it('bug 689482', function () {
        try {
            WarnLib.throw_unpaired();
            fail();
        } catch (e) {
            expect(e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_FOUND)).toBeFalsy();
        }
    });

    const WhateverImpl = GObject.registerClass({
        Implements: [WarnLib.Whatever],
    }, class WhateverImpl extends GObject.Object {
        vfunc_do_moo(x) {
            expect(x).toEqual(5);
            this.mooCalled = true;
        }

        vfunc_do_boo(x) {
            expect(x).toEqual(6);
            this.booCalled = true;
        }
    });

    it('calls vfuncs with unnamed parameters', function () {
        const o = new WhateverImpl();
        o.do_moo(5, null);
        o.do_boo(6, null);
        expect(o.mooCalled).toBeTruthy();  // spies don't work on vfuncs
        expect(o.booCalled).toBeTruthy();
    });

    it('handles enum members that start with a digit', function () {
        expect(WarnLib.NumericEnum['1ST']).toEqual(1);
    });
});
