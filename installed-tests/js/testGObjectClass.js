// -*- mode: js; indent-tabs-mode: nil -*-

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
        if (!(cancellable instanceof Gio.Cancellable))
            throw 'Bad argument';

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
    },

    _instance_init: function() {
        this.foo = true;
    }
});

describe('GObject class', function () {
    let myInstance;
    beforeEach(function () {
        myInstance = new MyObject();
    });

    it('constructs with default values for properties', function () {
        expect(myInstance.readwrite).toEqual('foo');
        expect(myInstance.readonly).toEqual('bar');
        expect(myInstance.construct).toEqual('default');
    });

    it('constructs with a hash of property values', function () {
        let myInstance2 = new MyObject({ readwrite: 'baz', construct: 'asdf' });
        expect(myInstance2.readwrite).toEqual('baz');
        expect(myInstance2.readonly).toEqual('bar');
        expect(myInstance2.construct).toEqual('asdf');
    });

    const ui = '<interface> \
                <object class="Gjs_MyObject" id="MyObject"> \
                  <property name="readwrite">baz</property> \
                  <property name="construct">quz</property> \
                </object> \
              </interface>';

    it('constructs with property values from Gtk.Builder', function () {
        let builder = Gtk.Builder.new_from_string(ui, -1);
        let myInstance3 = builder.get_object('MyObject');
        expect(myInstance3.readwrite).toEqual('baz');
        expect(myInstance3.readonly).toEqual('bar');
        expect(myInstance3.construct).toEqual('quz');
    });

    // the following would (should) cause a CRITICAL:
    // myInstance.readonly = 'val';
    // myInstance.construct = 'val';

    it('has a notify signal', function () {
        let notifySpy = jasmine.createSpy('notifySpy');
        myInstance.connect('notify::readonly', notifySpy);

        myInstance.notify_prop();
        myInstance.notify_prop();

        expect(notifySpy).toHaveBeenCalledTimes(2);
    });

    it('can define its own signals', function () {
        let emptySpy = jasmine.createSpy('emptySpy');
        myInstance.connect('empty', emptySpy);
        myInstance.emit_empty();

        expect(emptySpy).toHaveBeenCalled();
        expect(myInstance.empty_called).toBeTruthy();
    });

    it('passes emitted arguments to signal handlers', function () {
        let minimalSpy = jasmine.createSpy('minimalSpy');
        myInstance.connect('minimal', minimalSpy);
        myInstance.emit_minimal(7, 5);

        expect(minimalSpy).toHaveBeenCalledWith(myInstance, 7, 5);
    });

    it('can return values from signals', function () {
        let fullSpy = jasmine.createSpy('fullSpy').and.returnValue(42);
        myInstance.connect('full', fullSpy);
        let result = myInstance.emit_full();

        expect(fullSpy).toHaveBeenCalled();
        expect(result).toEqual(42);
    });

    it('does not call first-wins signal handlers after one returns a value', function () {
        let neverCalledSpy = jasmine.createSpy('neverCalledSpy');
        myInstance.connect('full', () => 42);
        myInstance.connect('full', neverCalledSpy);
        myInstance.emit_full();

        expect(neverCalledSpy).not.toHaveBeenCalled();
        expect(myInstance.full_default_handler_called).toBeFalsy();
    });

    it('gets the return value of the default handler', function () {
        let result = myInstance.emit_full();

        expect(myInstance.full_default_handler_called).toBeTruthy();
        expect(result).toEqual(79);
    });

    it('calls run-last default handler last', function () {
        let stack = [ ];
        let runLastSpy = jasmine.createSpy('runLastSpy')
            .and.callFake(() => { stack.push(1); });
        myInstance.connect('run-last', runLastSpy);
        myInstance.emit_run_last(() => { stack.push(2); });

        expect(stack).toEqual([1, 2]);
    });

    it("can inherit from something that's not GObject.Object", function () {
        // ...and still get all the goodies of GObject.Class
        let instance = new MyApplication({ application_id: 'org.gjs.Application' });
        let customSpy = jasmine.createSpy('customSpy');
        instance.connect('custom', customSpy);

        instance.emit_custom(73);
        expect(customSpy).toHaveBeenCalledWith(instance, 73);
    });

    it('can implement an interface', function () {
        let instance = new MyInitable();
        expect(instance.constructor.implements(Gio.Initable)).toBeTruthy();
    });

    it('can implement interface vfuncs', function () {
        let instance = new MyInitable();
        expect(instance.inited).toBeFalsy();

        instance.init(new Gio.Cancellable());
        expect(instance.inited).toBeTruthy();
    });

    it('can be a subclass', function () {
        let derived = new Derived();

        expect(derived instanceof Derived).toBeTruthy();
        expect(derived instanceof MyObject).toBeTruthy();

        expect(derived.readwrite).toEqual('yes');
    });

    it('calls its _instance_init() function while chaining up in constructor', function () {
        let instance = new MyCustomInit();
        expect(instance.foo).toBeTruthy();
    });

    it('can have an interface-valued property', function () {
        const InterfacePropObject = new Lang.Class({
            Name: 'InterfacePropObject',
            Extends: GObject.Object,
            Properties: {
                'file': GObject.ParamSpec.object('file', 'File', 'File',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    Gio.File.$gtype)
            },
        });
        let file = Gio.File.new_for_path('dummy');
        expect(() => new InterfacePropObject({ file: file })).not.toThrow();
    });

    it('can override a property from the parent class', function () {
        const OverrideObject = new Lang.Class({
            Name: 'OverrideObject',
            Extends: MyObject,
            Properties: {
                'readwrite': GObject.ParamSpec.override('readwrite', MyObject),
            },
            get readwrite() {
                return this._subclass_readwrite;
            },
            set readwrite(val) {
                this._subclass_readwrite = 'subclass' + val;
            },
        });
        let obj = new OverrideObject();
        obj.readwrite = 'foo';
        expect(obj.readwrite).toEqual('subclassfoo');
    });

    it('cannot override a non-existent property', function () {
        expect(() => new Lang.Class({
            Name: 'BadOverride',
            Extends: GObject.Object,
            Properties: {
                'nonexistent': GObject.ParamSpec.override('nonexistent', GObject.Object),
            },
        })).toThrow();
    });
});
