// application/javascript;version=1.8 -*- mode: js; indent-tabs-mode: nil -*-

if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(window); };
}

const Lang = imports.lang;
const GObject = imports.gi.GObject;
const Gio = imports.gi.Gio;

const MyObject = new GObject.Class({
    Name: 'MyObject',
    Properties: {
        'readwrite': GObject.ParamSpec.string('readwrite', 'ParamReadwrite',
                                              'A read write parameter',
                                              GObject.ParamFlags.READABLE | GObject.ParamFlags.WRITABLE,
                                              ''),
        'readonly': GObject.ParamSpec.string('readonly', 'ParamReadonly',
                                             'A readonly parameter',
                                             GObject.ParamFlags.READABLE,
                                             ''),

        'construct': GObject.ParamSpec.string('construct', 'ParamConstructOnly',
                                              'A readwrite construct-only parameter',
                                              GObject.ParamFlags.READABLE | GObject.ParamFlags.WRITABLE | GObject.ParamFlags.CONSTRUCT_ONLY,
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
        assertTrue(cancellable instanceof Gio.Cancellable);

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

function testGObjectClass() {
    let myInstance = new MyObject();

    assertEquals('foo', myInstance.readwrite);
    assertEquals('bar', myInstance.readonly);
    assertEquals('default', myInstance.construct);

    let myInstance2 = new MyObject({ readwrite: 'baz', construct: 'asdf' });

    assertEquals('baz', myInstance2.readwrite);
    assertEquals('bar', myInstance2.readonly);
    assertEquals('asdf', myInstance2.construct);

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

    assertEquals(2, counter);
}

function testSignals() {
    let myInstance = new MyObject();
    let ok = false;

    myInstance.connect('empty', function() {
        ok = true;
    });
    myInstance.emit_empty();

    assertEquals(true, ok);
    assertEquals(true, myInstance.empty_called);

    let args = [ ];
    myInstance.connect('minimal', function(emitter, one, two) {
        args.push(one);
        args.push(two);

        return true;
    });
    myInstance.emit_minimal(7, 5);

    assertEquals(7, args[0]);
    assertEquals(5, args[1]);

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
    result = myInstance.emit_full();

    assertEquals(true, ok);
    assertEquals(42, result);

    let stack = [ ];
    myInstance.connect('run-last', function() {
        stack.push(1);
    });
    myInstance.emit_run_last(function() {
        stack.push(2);
    });

    assertEquals(1, stack[0]);
    assertEquals(2, stack[1]);
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
    assertEquals(73, v);
}

function testInterface() {
    let instance = new MyInitable();
    assertEquals(false, instance.inited);

    instance.init(new Gio.Cancellable);
    assertEquals(true, instance.inited);

    // assertTrue(instance instanceof Gio.Initable)
}

function testDerived() {
    let derived = new Derived();

    assertTrue(derived instanceof Derived);
    assertTrue(derived instanceof MyObject);

    assertEquals('yes', derived.readwrite);
}

gjstestRun();
