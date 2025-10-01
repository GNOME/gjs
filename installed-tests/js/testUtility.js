// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Philip Chimento <philip.chimento@gmail.com>

import Utility from 'gi://Utility';

// A small library, part of gobject-introspection-tests, mainly used as a
// dependency of other tests.

describe('Utility callback', function () {
    it('recognizes callback correctly', function () {
        Utility.dir_foreach('/path/', () => {});
    });
});

describe('Utility object', function () {
    it('recognizes callback correctly', function () {
        const o = new Utility.Object();
        o.watch_dir('/path/', () => {});
    });
});

describe('Utility unions/structs', function () {
    xit('TaggedValue', function () {
        const t = new Utility.TaggedValue();
        t.tag = 0xff;
        expect(t.tag).toBe(0xff);
        expect(t.value.v_pointer).toBeNull();
        t.value.v_integer = 0x7fff_ffff;
        expect(t.value.v_integer).toBe(0x7fff_ffff);
        t.value.v_double = Math.PI;
        expect(t.value.v_double).toBe(Math.PI);
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/569');

    xit('Byte', function () {
        const b = new Utility.Byte();
        b.value = 0xcd;
        expect(b.parts.first_nibble).toBe(0xc);
        expect(b.parts.second_nibble).toBe(0xd);
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/569');

    it('Buffer', function () {
        const b = new Utility.Buffer();
        expect(b.length).toBe(0);
        expect(b.data).toBe(null);
    });

    xit('Struct', function () {
        const s = new Utility.Struct();
        s.field = 42;
        s.bitfield1 = 0xf;
        s.bitfield2 = 0x0;
        expect(s.field).toBe(42);
        expect(s.bitfield1).toBe(7);
        expect(s.bitfield2).toBe(0);
        expect(s.data).toBeInstanceOf(Uint8Array);
    }).pend('Bitfields not supported. Open an issue if you need this');

    it('Union', function () {
        const u = new Utility.Union();
        expect(u.pointer).toBeNull();
        u.integer = 0x7fff_ffff;
        expect(u.integer).toBe(0x7fff_ffff);
        u.real = Math.PI;
        expect(u.real).toBe(Math.PI);
    });
});

describe('Utility enums/flags', function () {
    it('enum', function () {
        expect(Utility.EnumType).toEqual(jasmine.objectContaining({
            A: 0,
            B: 1,
            C: 2,
        }));
    });

    it('flags', function () {
        expect(Utility.FlagType).toEqual(jasmine.objectContaining({
            A: 1,
            B: 2,
            C: 4,
        }));
    });
});
