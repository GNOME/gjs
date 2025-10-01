// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Red Hat, Inc.
// SPDX-FileCopyrightText: 2015 Endless Mobile, Inc.

import GIMarshallingTests from 'gi://GIMarshallingTests';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';

const Foo = GObject.registerClass({
    Properties: {
        'prop': GObject.ParamSpec.string('prop', '', '', GObject.ParamFlags.READWRITE, ''),
    },
}, class Foo extends GObject.Object {
    set prop(v) {
        throw new Error('set');
    }

    get prop() {
        throw new Error('get');
    }
});

const Bar = GObject.registerClass({
    Properties: {
        'prop': GObject.ParamSpec.string('prop', '', '',
            GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT, ''),
    },
}, class Bar extends GObject.Object {});

describe('Exceptions', function () {
    it('are thrown from property setter', function () {
        let foo = new Foo();
        expect(() => (foo.prop = 'bar')).toThrowError(/set/);
    });

    it('are thrown from property getter', function () {
        let foo = new Foo();
        expect(() => foo.prop).toThrowError(/get/);
    });

    // FIXME: In the next cases the errors aren't thrown but logged

    it('are logged from constructor', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: set*');

        new Foo({prop: 'bar'});

        GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
            'testExceptionInPropertySetterFromConstructor');
    });

    it('are logged from property setter with binding', function () {
        let foo = new Foo();
        let bar = new Bar();

        bar.bind_property('prop',
            foo, 'prop',
            GObject.BindingFlags.DEFAULT);
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: set*');

        // wake up the binding so that g_object_set() is called on foo
        bar.notify('prop');

        GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
            'testExceptionInPropertySetterWithBinding');
    });

    it('are logged from property getter with binding', function () {
        let foo = new Foo();
        let bar = new Bar();

        foo.bind_property('prop',
            bar, 'prop',
            GObject.BindingFlags.DEFAULT);
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: get*');

        // wake up the binding so that g_object_get() is called on foo
        foo.notify('prop');

        GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
            'testExceptionInPropertyGetterWithBinding');
    });
});

describe('logError', function () {
    afterEach(function () {
        GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js',
            0, 'testGErrorMessages');
    });

    it('logs a warning for a GError', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: *');
        try {
            let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
            file.read(null);
        } catch (e) {
            logError(e);
        }
    });

    it('logs a warning with a message if given', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: a message\nmarker@*');
        try {
            throw new Gio.IOErrorEnum({message: 'a message', code: 0});
        } catch (e) {
            logError(e);
        }
    });

    it('also logs an error for a created GError that is not thrown', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: a message\nmarker@*');
        logError(new Gio.IOErrorEnum({message: 'a message', code: 0}));
    });

    it('logs an error created with the GLib.Error constructor', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: a message\nmarker@*');
        logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'));
    });

    it('logs the quark for a JS-created GError type', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: GLib.Error my-error: a message\nmarker@*');
        logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'));
    });

    it('logs with stack for a GError created from a C struct', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: GLib.Error gi-marshalling-tests-gerror-domain: gi-marshalling-tests-gerror-message\nmarker@*');
        logError(GIMarshallingTests.gerror_return());
    });

    // Now with prefix

    it('logs an error with a prefix if given', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: Gio.IOErrorEnum: *');
        try {
            let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
            file.read(null);
        } catch (e) {
            logError(e, 'prefix');
        }
    });

    it('logs an error with prefix and message', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: Gio.IOErrorEnum: a message\nmarker@*');
        try {
            throw new Gio.IOErrorEnum({message: 'a message', code: 0});
        } catch (e) {
            logError(e, 'prefix');
        }
    });

    describe('Syntax Error', function () {
        function throwSyntaxError() {
            Reflect.parse('!@#$%^&');
        }

        it('logs a SyntaxError', function () {
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                'JS ERROR: SyntaxError:*');
            try {
                throwSyntaxError();
            } catch (e) {
                logError(e);
            }
        });

        it('logs a stack trace with the SyntaxError', function () {
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                'JS ERROR: SyntaxError:*throwSyntaxError@*');
            try {
                throwSyntaxError();
            } catch (e) {
                logError(e);
            }
        });
    });

    it('logs an error with cause', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Error: an error\nmarker@*Caused by: Gio.IOErrorEnum: another error\nmarker2@*');
        function marker2() {
            return new Gio.IOErrorEnum({message: 'another error', code: 0});
        }
        logError(new Error('an error', {cause: marker2()}));
    });

    it('logs a GError with cause', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: an error\nmarker@*Caused by: Error: another error\nmarker2@*');
        function marker2() {
            return new Error('another error');
        }
        const e = new Gio.IOErrorEnum({message: 'an error', code: 0});
        e.cause = marker2();
        logError(e);
    });

    it('logs an error with non-object cause', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Error: an error\n*Caused by: 3');
        logError(new Error('an error', {cause: 3}));
    });

    it('logs an error with a cause tree', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Error: one\n*Caused by: Error: two\n*Caused by: Error: three\n*');
        const three = new Error('three');
        const two = new Error('two', {cause: three});
        logError(new Error('one', {cause: two}));
    });

    it('logs an error with cyclical causes', function () {
        // We cannot assert here with GLib.test_expect_message that the * at the
        // end of the string doesn't match more causes, but at least the idea is
        // that it shouldn't go into an infinite loop
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Error: one\n*Caused by: Error: two\n*');
        const one = new Error('one');
        one.cause = new Error('two', {cause: one});
        logError(one);
    });
});

describe('Exception from function with too few arguments', function () {
    it('contains the full function name', function () {
        expect(() => GLib.get_locale_variants())
            .toThrowError(/GLib\.get_locale_variants/);
    });

    it('contains the full method name', function () {
        let file = Gio.File.new_for_path('foo');
        expect(() => file.read()).toThrowError(/Gio\.File\.read/);
    });
});

describe('thrown GError', function () {
    let err;
    beforeEach(function () {
        try {
            let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
            file.read(null);
        } catch (x) {
            err = x;
        }
    });

    it('is an instance of error enum type', function () {
        expect(err).toEqual(jasmine.any(Gio.IOErrorEnum));
    });

    it('matches error domain and code', function () {
        expect(err.matches(Gio.io_error_quark(), Gio.IOErrorEnum.NOT_FOUND))
            .toBeTruthy();
    });

    it('has properties for domain and code', function () {
        expect(err.domain).toEqual(Gio.io_error_quark());
        expect(err.code).toEqual(Gio.IOErrorEnum.NOT_FOUND);
    });
});

describe('GError.new_literal', function () {
    it('constructs a valid GLib.Error', function () {
        const e = GLib.Error.new_literal(
            Gio.IOErrorEnum, Gio.IOErrorEnum.FAILED, 'message');
        expect(e instanceof GLib.Error).toBeTruthy();
        expect(e.code).toEqual(Gio.IOErrorEnum.FAILED);
        expect(e.message).toEqual('message');
    });
    it('does not accept invalid domains', function () {
        expect(() => GLib.Error.new_literal(0, 0, 'message'))
            .toThrowError(/0 is not a valid domain/);
    });
});

describe('Interoperation with Error.isError', function () {
    it('thrown GError', function () {
        let err;
        try {
            const file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
            file.read(null);
        } catch (e) {
            err = e;
        }
        expect(Error.isError(err)).toBeTruthy();
    });

    xit('returned GError', function () {
        const err = GIMarshallingTests.gerror_return();
        expect(Error.isError(err)).toBeTruthy();
    }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/700');

    it('created GError', function () {
        const err = new Gio.IOErrorEnum({message: 'a message', code: 0});
        expect(Error.isError(err)).toBeTruthy();
    });

    it('GError created with the GLib.Error constructor', function () {
        const err = new GLib.Error(Gio.IOErrorEnum, 0, 'a message');
        expect(Error.isError(err)).toBeTruthy();
    });

    it('GError created with GLib.Error.new_literal', function () {
        const err = GLib.Error.new_literal(
            Gio.IOErrorEnum, Gio.IOErrorEnum.FAILED, 'message');
        expect(Error.isError(err)).toBeTruthy();
    });

    it('not an error', function () {
        const err = new GIMarshallingTests.BoxedStruct();
        expect(Error.isError(err)).toBeFalsy();
    });
});
