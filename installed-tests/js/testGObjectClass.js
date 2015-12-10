// -*- mode: js; indent-tabs-mode: nil -*-

const JSUnit = imports.jsUnit;
const Lang = imports.lang;
const GObject = imports.gi.GObject;
const Gio = imports.gi.Gio;
const Gtk = imports.gi.Gtk;

const MyObject = new GObject.Class({
    Name: 'MyObject',
    Properties: {
        'readwrite': GObject.ParamSpec.string('readwrite', 'ParamReadwrite',
                                              'A read write parameter',
                                              GObject.ParamFlags.READWRITE,
                                              ''),
        'readonly': GObject.ParamSpec.string('readonly', 'ParamReadonly',
                                             'A readonly parameter',
                                             GObject.ParamFlags.READABLE,
                                             ''),

        'construct': GObject.ParamSpec.string('construct', 'ParamConstructOnly',
                                              'A readwrite construct-only parameter',
                                              GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                                              'default')
    },
    Signals: {
        'empty': { },
        'minimal': { param_types: [ GObject.TYPE_INT, GObject.TYPE_INT ] },
        'full': { flags: GObject.SignalFlags.RUN_LAST, accumulator: GObject.AccumulatorType.FIRST_WINS,
                  return_type: GObject.TYPE_INT, param_types: [ ] },
        'run-last': { flags: GObject.SignalFlags.RUN_LAST },
        'detailed': { flags: GObject.SignalFlags.RUN_FIRST | GObject.SignalFlags.DETAILED, param_types: [ GObject.TYPE_STRING ] }
    },

    _init: function(props) {
        // check that it's safe to set properties before
        // chaining up (priv is NULL at this point, remember)
        this._readwrite = 'foo';
        this._readonly = 'bar';
        this._constructProp = null;
        this._constructCalled = false;

        this.parent(props);
    },

    get readwrite() {
        return this._readwrite;
    },

    set readwrite(val) {
        if (val == 'ignore')
            return;

        this._readwrite = val;
    },

    get readonly() {
        return this._readonly;
    },

    set readonly(val) {
        // this should never be called
        this._readonly = 'bogus';
    },

    get construct() {
        return this._constructProp;
    },

    set construct(val) {
        // this should be called at most once
        if (this._constructCalled)
            throw Error('Construct-Only property set more than once');

        this._constructProp = val;
        this._constructCalled = true;
    },

    notify_prop: function() {
        this._readonly = 'changed';

        this.notify('readonly');
    },

    emit_empty: function() {
        this.emit('empty');
    },

    emit_minimal: function(one, two) {
        this.emit('minimal', one, two);
    },

    emit_full: function() {
        return this.emit('full');
    },

    emit_detailed: function() {
        this.emit('detailed::one');
        this.emit('detailed::two');
    },

    emit_run_last: function(callback) {
        this._run_last_callback = callback;
        this.emit('run-last');
    },

    on_run_last: function() {
        this._run_last_callback();
    },

    on_empty: function() {
        this.empty_called = true;
    },

    on_full: function() {
        this.full_default_handler_called = true;
        return 79;
    }
});

const MyApplication = new Lang.Class({
    Name: 'MyApplication',
    Extends: Gio.Application,
    Signals: { 'custom': { param_types: [ GObject.TYPE_INT ] } },

    _init: function(params) {
        this.parent(params);
    },

    emit_custom: function(n) {
        this.emit('custom', n);
    }
});

const MyInitable = new Lang.Class({
    Name: 'MyInitable',
    Extends: GObject.Object,
    Implements: [ Gio.Initable ],

    _init: function(params) {
        this.parent(params);

        this.inited = false;
    },

    vfunc_init: function(cancellable) { // error?
        JSUnit.assertTrue(cancellable instanceof Gio.Cancellable);

        this.inited = true;
    }
});

const Derived = new Lang.Class({
    Name: 'Derived',
    Extends: MyObject,

    _init: function() {
        this.parent({ readwrite: 'yes' });
    }
});

const MyCustomInit = new Lang.Class({
    Name: 'MyCustomInit',
    Extends: GObject.Object,

    _init: function() {
        this.foo = false;

        this.parent();

        JSUnit.assert(this.foo);
    },

    _instance_init: function() {
        this.foo = true;
    }
});

function testGObjectClass() {
    let myInstance = new MyObject();

    JSUnit.assertEquals('foo', myInstance.readwrite);
    JSUnit.assertEquals('bar', myInstance.readonly);
    JSUnit.assertEquals('default', myInstance.construct);

    let myInstance2 = new MyObject({ readwrite: 'baz', construct: 'asdf' });

    JSUnit.assertEquals('baz', myInstance2.readwrite);
    JSUnit.assertEquals('bar', myInstance2.readonly);
    JSUnit.assertEquals('asdf', myInstance2.construct);

    let ui = '<interface> \
                <object class="Gjs_MyObject" id="MyObject"> \
                  <property name="readwrite">baz</property> \
                  <property name="construct">quz</property> \
                </object> \
              </interface>';
    let builder = Gtk.Builder.new_from_string(ui, -1);
    let myInstance3 = builder.get_object('MyObject');
    JSUnit.assertEquals('baz', myInstance3.readwrite);
    JSUnit.assertEquals('bar', myInstance3.readonly);
    JSUnit.assertEquals('quz', myInstance3.construct);

    // the following would (should) cause a CRITICAL:
    // myInstance.readonly = 'val';
    // myInstance.construct = 'val';
}

function testNotify() {
    let myInstance = new MyObject();
    let counter = 0;

    myInstance.connect('notify::readonly', function(obj) {
        if (obj.readonly == 'changed')
            counter++;
    });

    myInstance.notify_prop();
    myInstance.notify_prop();

    JSUnit.assertEquals(2, counter);
}

function testSignals() {
    let myInstance = new MyObject();
    let ok = false;

    myInstance.connect('empty', function() {
        ok = true;
    });
    myInstance.emit_empty();

    JSUnit.assertEquals(true, ok);
    JSUnit.assertEquals(true, myInstance.empty_called);

    let args = [ ];
    myInstance.connect('minimal', function(emitter, one, two) {
        args.push(one);
        args.push(two);

        return true;
    });
    myInstance.emit_minimal(7, 5);

    JSUnit.assertEquals(7, args[0]);
    JSUnit.assertEquals(5, args[1]);

    ok = true;
    myInstance.connect('full', function() {
        ok = true;

        return 42;
    });
    myInstance.connect('full', function() {
        // this should never be called
        ok = false;

        return -1;
    });
    let result = myInstance.emit_full();

    JSUnit.assertEquals(true, ok);
    JSUnit.assertUndefined(myInstance.full_default_handler_called);
    JSUnit.assertEquals(42, result);

    let stack = [ ];
    myInstance.connect('run-last', function() {
        stack.push(1);
    });
    myInstance.emit_run_last(function() {
        stack.push(2);
    });

    JSUnit.assertEquals(1, stack[0]);
    JSUnit.assertEquals(2, stack[1]);
}

function testSubclass() {
    // test that we can inherit from something that's not
    // GObject.Object and still get all the goodies of
    // GObject.Class

    let instance = new MyApplication({ application_id: 'org.gjs.Application' });
    let v;

    instance.connect('custom', function(app, num) {
        v = num;
    });

    instance.emit_custom(73);
    JSUnit.assertEquals(73, v);
}

function testInterface() {
    let instance = new MyInitable();
    JSUnit.assertEquals(false, instance.inited);

    instance.init(new Gio.Cancellable);
    JSUnit.assertEquals(true, instance.inited);

    JSUnit.assertTrue(instance.constructor.implements(Gio.Initable));
}

function testDerived() {
    let derived = new Derived();

    JSUnit.assertTrue(derived instanceof Derived);
    JSUnit.assertTrue(derived instanceof MyObject);

    JSUnit.assertEquals('yes', derived.readwrite);
}

function testInstanceInit() {
    new MyCustomInit();
}

function testClassCanHaveInterfaceProperty() {
    const InterfacePropObject = new Lang.Class({
        Name: 'InterfacePropObject',
        Extends: GObject.Object,
        Properties: {
            'file': GObject.ParamSpec.object('file', 'File', 'File',
                GObject.ParamFlags.READABLE | GObject.ParamFlags.WRITABLE | GObject.ParamFlags.CONSTRUCT_ONLY,
                Gio.File.$gtype)
        }
    });
    let obj = new InterfacePropObject({ file: Gio.File.new_for_path('dummy') });
}

function testClassCanOverrideParentClassProperty() {
    const OverrideObject = new Lang.Class({
        Name: 'OverrideObject',
        Extends: MyObject,
        Properties: {
            'readwrite': GObject.ParamSpec.override('readwrite', MyObject)
        },
        get readwrite() {
            return this._subclass_readwrite;
        },
        set readwrite(val) {
            this._subclass_readwrite = 'subclass' + val;
        }
    });
    let obj = new OverrideObject();
    obj.readwrite = 'foo';
    JSUnit.assertEquals(obj.readwrite, 'subclassfoo');
}

function testClassCannotOverrideNonexistentProperty() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'BadOverride',
        Extends: GObject.Object,
        Properties: {
            'nonexistent': GObject.ParamSpec.override('nonexistent', GObject.Object)
        }
    }));
}

function testDefaultHandler() {
    let myInstance = new MyObject();
    let result = myInstance.emit_full();

    JSUnit.assertEquals(true, myInstance.full_default_handler_called);
    JSUnit.assertEquals(79, result);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
