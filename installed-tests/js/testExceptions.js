const JSUnit = imports.jsUnit;
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

function testExceptionInPropertySetter() {
    let foo = new Foo();
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: Error: set*');

    try {
	foo.prop = 'bar';
    } catch (e) {
	logError(e);
    }

    GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
                                                'testExceptionInPropertySetter');
}

function testExceptionInPropertyGetter() {
    let foo = new Foo();
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: Error: get*');

    try {
	let bar = foo.prop;
    } catch (e) {
	logError(e);
    }

    GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
                                                'testExceptionInPropertyGetter');
}

function testExceptionInPropertySetterFromConstructor() {
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: Error: set*');

    try {
	let foo = new Foo({ prop: 'bar' });
    } catch (e) {
	logError(e);
    }

    GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
                                                'testExceptionInPropertySetterFromConstructor');
}

function testExceptionInPropertySetterWithBinding() {
    let foo = new Foo();
    let bar = new Bar();

    bar.bind_property('prop',
		      foo, 'prop',
		      GObject.BindingFlags.DEFAULT);
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: Error: set*');

    try {
	// wake up the binding so that g_object_set() is called on foo
	bar.notify('prop');
    } catch (e) {
	logError(e);
    }

    GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
                                                'testExceptionInPropertySetterWithBinding');
}

function testExceptionInPropertyGetterWithBinding() {
    let foo = new Foo();
    let bar = new Bar();

    foo.bind_property('prop',
		      bar, 'prop',
		      GObject.BindingFlags.DEFAULT);
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: Error: get*');

    try {
	// wake up the binding so that g_object_get() is called on foo
	foo.notify('prop');
    } catch (e) {
	logError(e);
    }

    GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
                                                'testExceptionInPropertyGetterWithBinding');
}

function testGErrorMessages() {
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: Gio.IOErrorEnum: *');
    try {
        let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
        file.read(null);
    } catch(e) {
        logError(e);
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    try {
        throw new Gio.IOErrorEnum({ message: 'a message', code: 0 });
    } catch(e) {
        logError(e);
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    logError(new Gio.IOErrorEnum({ message: 'a message', code: 0 }));

    // No stack for GLib.Error constructor
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: Gio.IOErrorEnum: a message');
    logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'));

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: GLib.Error my-error: a message');
    logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'));

    // Now with prefix

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: prefix: Gio.IOErrorEnum: *');
    try {
        let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
        file.read(null);
    } catch(e) {
        logError(e, 'prefix');
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: prefix: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    try {
        throw new Gio.IOErrorEnum({ message: 'a message', code: 0 });
    } catch(e) {
        logError(e, 'prefix');
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: prefix: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    logError(new Gio.IOErrorEnum({ message: 'a message', code: 0 }), 'prefix');

    // No stack for GLib.Error constructor
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: prefix: Gio.IOErrorEnum: a message');
    logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'), 'prefix');

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        'JS ERROR: prefix: GLib.Error my-error: a message');
    logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'), 'prefix');

    GLib.test_assert_expected_messages_internal('Gjs', 'testExceptions.js', 0,
        'testGErrorMessages');
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
