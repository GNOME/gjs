// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Pavel Vasin <rat4vier@gmail.com>
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2017 Claudio André <claudioandre.br@gmail.com>
// SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2019 Canonical, Ltd.

const System = imports.system;
const {Gio, GObject} = imports.gi;

describe('System.addressOf()', function () {
    it('gives different results for different objects', function () {
        let a = {some: 'object'};
        let b = {different: 'object'};
        expect(System.addressOf(a)).not.toEqual(System.addressOf(b));
    });
});

describe('System.version', function () {
    it('gives a plausible number', function () {
        expect(System.version).not.toBeLessThan(14700);
        expect(System.version).toBeLessThan(20000);
    });
});

describe('System.versionString', function () {
    it('gives a correctly formatted string', function () {
        expect(System.versionString).toMatch(/1.[0-9]{2}.[0-9]/);
    });
});

describe('System.refcount()', function () {
    it('gives the correct number', function () {
        let o = new GObject.Object({});
        expect(System.refcount(o)).toEqual(1);
    });
});

describe('System.addressOfGObject()', function () {
    it('gives different results for different objects', function () {
        let a = new GObject.Object({});
        let b = new GObject.Object({});
        expect(System.addressOfGObject(a)).toEqual(System.addressOfGObject(a));
        expect(System.addressOfGObject(a)).not.toEqual(System.addressOfGObject(b));
    });

    it('throws for non GObject objects', function () {
        expect(() => System.addressOfGObject({}))
            .toThrowError(/Object 0x[a-f0-9]+ is not a GObject/);
    });
});

describe('System.gc()', function () {
    it('does not crash the application', function () {
        expect(System.gc).not.toThrow();
    });
});

describe('System.dumpHeap()', function () {
    it('throws but does not crash when given a nonexistent path', function () {
        expect(() => System.dumpHeap('/does/not/exist')).toThrow();
    });
});

describe('System.dumpMemoryInfo()', function () {
    it('', function () {
        expect(() => System.dumpMemoryInfo('memory.md')).not.toThrow();
        expect(() => Gio.File.new_for_path('memory.md').delete(null)).not.toThrow();
    });

    it('throws but does not crash when given a nonexistent path', function () {
        expect(() => System.dumpMemoryInfo('/does/not/exist')).toThrowError(/\/does\/not\/exist/);
    });
});

describe('System.programPath', function () {
    it('is null when executed from minijasmine', function () {
        expect(System.programPath).toBe(null);
    });
});

describe('System.programArgs', function () {
    it('System.programArgs is an array', function () {
        expect(Array.isArray(System.programArgs)).toBeTruthy();
    });

    it('modifications persist', function () {
        System.programArgs.push('--foo');
        expect(System.programArgs.pop()).toBe('--foo');
    });

    it('System.programArgs is equal to ARGV', function () {
        expect(System.programArgs).toEqual(ARGV);
        ARGV.push('--foo');
        expect(System.programArgs.pop()).toBe('--foo');
    });
});
