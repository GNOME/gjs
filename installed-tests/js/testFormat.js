// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Red Hat, Inc.

// eslint-disable-next-line no-restricted-properties
const Format = imports.format;
String.prototype.format = Format.format;

describe('imports.format', function () {
    it('escapes % with another % character', function () {
        expect('%d%%'.format(10)).toEqual('10%');
    });

    it('formats a single string argument', function () {
        expect('%s'.format('Foo')).toEqual('Foo');
    });

    it('formats two string arguments', function () {
        expect('%s %s'.format('Foo', 'Bar')).toEqual('Foo Bar');
    });

    it('formats two swapped string arguments', function () {
        expect('%2$s %1$s'.format('Foo', 'Bar')).toEqual('Bar Foo');
    });

    it('formats a number in base 10', function () {
        expect('%d'.format(42)).toEqual('42');
    });

    it('formats a number in base 16', function () {
        expect('%x'.format(42)).toEqual('2a');
    });

    it('formats a floating point number with no precision', function () {
        expect('%f'.format(0.125)).toEqual('0.125');
    });

    it('formats a floating point number with precision 2', function () {
        expect('%.2f'.format(0.125)).toEqual('0.13');
    });

    it('pads with zeroes', function () {
        let zeroFormat = '%04d';
        expect(zeroFormat.format(1)).toEqual('0001');
        expect(zeroFormat.format(10)).toEqual('0010');
        expect(zeroFormat.format(100)).toEqual('0100');
    });

    it('pads with spaces', function () {
        let spaceFormat = '%4d';
        expect(spaceFormat.format(1)).toEqual('   1');
        expect(spaceFormat.format(10)).toEqual('  10');
        expect(spaceFormat.format(100)).toEqual(' 100');
    });

    it('throws an error when given incorrect modifiers for the conversion type', function () {
        expect(() => '%z'.format(42)).toThrow();
        expect(() => '%.2d'.format(42)).toThrow();
        expect(() => '%Ix'.format(42)).toThrow();
    });

    it('throws an error when incorrectly instructed to swap arguments', function () {
        expect(() => '%2$d %d %1$d'.format(1, 2, 3)).toThrow();
    });
});
