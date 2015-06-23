const JSUnit = imports.jsUnit;
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
        'prop': GObject.ParamSpec.string('prop', '', '', GObject.ParamFlags.READWRITE, '')
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

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
