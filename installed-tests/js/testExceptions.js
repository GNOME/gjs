const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
const Lang = imports.lang;

const Foo = new Lang.Class({
    Name: 'Foo',
    Extends: GObject.Object,
    Properties: {
        'prop': GObject.ParamSpec.string('prop', '', '', GObject.ParamFlags.READWRITE, '')
    },

    set prop(v) {
	throw new Error('set');
    },

    get prop() {
	throw new Error('get');
    }
});

const Bar = new Lang.Class({
    Name: 'Bar',
    Extends: GObject.Object,
    Properties: {
        'prop': GObject.ParamSpec.string('prop', '', '',
            GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT, ''),
    }
});

describe('Exceptions', function () {
    it('are thrown from property setter', function () {
        let foo = new Foo();
        expect(() => foo.prop = 'bar').toThrowError(/set/);
    });

    it('are thrown from property getter', function () {
        let foo = new Foo();
        expect(() => foo.prop).toThrowError(/get/);
    });

    // FIXME: In the next cases the errors aren't thrown but logged

    it('are logged from constructor', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Error: set*');

        new Foo({ prop: 'bar' });

        GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
            'testExceptionInPropertySetterFromConstructor');
    });

    it('are logged from property setter with binding', function () {
        let foo = new Foo();
        let bar = new Bar();

        bar.bind_property('prop',
            foo, 'prop',
            GObject.BindingFlags.DEFAULT);
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
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
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
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
        } catch(e) {
            logError(e);
        }
    });

    it('logs a warning with a message if given', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: a message\nmarker@*');
        try {
            throw new Gio.IOErrorEnum({ message: 'a message', code: 0 });
        } catch(e) {
            logError(e);
        }
    });

    it('also logs an error for a created GError that is not thrown', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: a message\nmarker@*');
        logError(new Gio.IOErrorEnum({ message: 'a message', code: 0 }));
    });

    it('logs an error with no stack trace for an error created with the GLib.Error constructor', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Gio.IOErrorEnum: a message');
        logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'));
    });

    it('logs the quark for a JS-created GError type', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: GLib.Error my-error: a message');
        logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'));
    });

    // Now with prefix

    it('logs an error with a prefix if given', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: Gio.IOErrorEnum: *');
        try {
            let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
            file.read(null);
        } catch(e) {
            logError(e, 'prefix');
        }
    });

    it('logs an error with prefix and message', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: Gio.IOErrorEnum: a message\nmarker@*');
        try {
            throw new Gio.IOErrorEnum({ message: 'a message', code: 0 });
        } catch(e) {
            logError(e, 'prefix');
        }
    });

    it('logs a non-thrown error with prefix', function marker() {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: Gio.IOErrorEnum: a message\nmarker@*');
        logError(new Gio.IOErrorEnum({ message: 'a message', code: 0 }), 'prefix');
    });

    it('logs a GLib.Error with prefix', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: Gio.IOErrorEnum: a message');
        logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'), 'prefix');
    });

    it('logs a JS-created GLib.Error with prefix', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: prefix: GLib.Error my-error: a message');
        logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'), 'prefix');
    });
});
