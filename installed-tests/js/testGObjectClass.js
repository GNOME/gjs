// -*- mode: js; indent-tabs-mode: nil -*-
imports.gi.versions.Gtk = '3.0';

const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
const Gtk = imports.gi.Gtk;

const MyObject = GObject.registerClass({
    Properties: {
        'readwrite': GObject.ParamSpec.string('readwrite', 'ParamReadwrite',
            'A read write parameter', GObject.ParamFlags.READWRITE, ''),
        'readonly': GObject.ParamSpec.string('readonly', 'ParamReadonly',
            'A readonly parameter', GObject.ParamFlags.READABLE, ''),
        'construct': GObject.ParamSpec.string('construct', 'ParamConstructOnly',
            'A readwrite construct-only parameter',
            GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
            'default'),
    },
    Signals: {
        'empty': {},
        'minimal': { param_types: [ GObject.TYPE_INT, GObject.TYPE_INT ] },
        'full': {
            flags: GObject.SignalFlags.RUN_LAST,
            accumulator: GObject.AccumulatorType.FIRST_WINS,
            return_type: GObject.TYPE_INT,
            param_types: [],
        },
        'run-last': { flags: GObject.SignalFlags.RUN_LAST },
        'detailed': {
            flags: GObject.SignalFlags.RUN_FIRST | GObject.SignalFlags.DETAILED,
            param_types: [ GObject.TYPE_STRING ],
        },
    },
}, class MyObject extends GObject.Object {
    get readwrite() {
        if (typeof this._readwrite === 'undefined')
            return 'foo';
        return this._readwrite;
    }

    set readwrite(val) {
        if (val == 'ignore')
            return;

        this._readwrite = val;
    }

    get readonly() {
        if (typeof this._readonly === 'undefined')
            return 'bar';
        return this._readonly;
    }

    set readonly(val) {
        // this should never be called
        void val;
        this._readonly = 'bogus';
    }

    get construct() {
        if (typeof this._constructProp === 'undefined')
            return null;
        return this._constructProp;
    }

    set construct(val) {
        // this should be called at most once
        if (this._constructCalled)
            throw Error('Construct-Only property set more than once');

        this._constructProp = val;
        this._constructCalled = true;
    }

    notify_prop() {
        this._readonly = 'changed';

        this.notify('readonly');
    }

    emit_empty() {
        this.emit('empty');
    }

    emit_minimal(one, two) {
        this.emit('minimal', one, two);
    }

    emit_full() {
        return this.emit('full');
    }

    emit_detailed() {
        this.emit('detailed::one');
        this.emit('detailed::two');
    }

    emit_run_last(callback) {
        this._run_last_callback = callback;
        this.emit('run-last');
    }

    on_run_last() {
        this._run_last_callback();
    }

    on_empty() {
        this.empty_called = true;
    }

    on_full() {
        this.full_default_handler_called = true;
        return 79;
    }
});

const MyAbstractObject = GObject.registerClass({
    GTypeFlags: GObject.TypeFlags.ABSTRACT
}, class MyAbstractObject extends GObject.Object {
});

const MyApplication = GObject.registerClass({
    Signals: { 'custom': { param_types: [ GObject.TYPE_INT ] } },
}, class MyApplication extends Gio.Application {
    emit_custom(n) {
        this.emit('custom', n);
    }
});

const MyInitable = GObject.registerClass({
    Implements: [ Gio.Initable ],
}, class MyInitable extends GObject.Object {
    vfunc_init(cancellable) {
        if (!(cancellable instanceof Gio.Cancellable))
            throw 'Bad argument';

        this.inited = true;
    }
});

const Derived = GObject.registerClass(class Derived extends MyObject {
    _init() {
        super._init({ readwrite: 'yes' });
    }
});

const Cla$$ = GObject.registerClass(class Cla$$ extends MyObject {});

const MyCustomInit = GObject.registerClass(class MyCustomInit extends GObject.Object {
    _instance_init() {
        this.foo = true;
    }
});

describe('GObject class with decorator', function () {
    let myInstance;
    beforeEach(function () {
        myInstance = new MyObject();
    });

    it('throws an error when not used with a GObject-derived class', function () {
        class Foo {}
        expect (() => GObject.registerClass(class Bar extends Foo {})).toThrow();
    });

    it('throws an error when used with an abstract class', function() {
        expect (() => new MyAbstractObject()).toThrow();
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

    const ui = `<interface>
                  <object class="Gjs_MyObject" id="MyObject">
                    <property name="readwrite">baz</property>
                    <property name="construct">quz</property>
                  </object>
                </interface>`;

    it('constructs with property values from Gtk.Builder', function () {
        let builder = Gtk.Builder.new_from_string(ui, -1);
        let myInstance3 = builder.get_object('MyObject');
        expect(myInstance3.readwrite).toEqual('baz');
        expect(myInstance3.readonly).toEqual('bar');
        expect(myInstance3.construct).toEqual('quz');
    });

    it('has a name', function () {
        expect(MyObject.name).toEqual('MyObject');
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
        expect(instance instanceof Gio.Initable).toBeTruthy();
        expect(instance instanceof Gio.AsyncInitable).toBeFalsy();

        // Old syntax, backwards compatible
        expect(instance.constructor.implements(Gio.Initable)).toBeTruthy();
        expect(instance.constructor.implements(Gio.AsyncInitable)).toBeFalsy();
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

    it('can have any valid class name', function () {
        let obj = new Cla$$();

        expect(obj instanceof Cla$$).toBeTruthy();
        expect(obj instanceof MyObject).toBeTruthy();
    });

    it('calls its _instance_init() function while chaining up in constructor', function () {
        let instance = new MyCustomInit();
        expect(instance.foo).toBeTruthy();
    });

    it('can have an interface-valued property', function () {
        const InterfacePropObject = GObject.registerClass({
            Properties: {
                'file': GObject.ParamSpec.object('file', 'File', 'File',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    Gio.File.$gtype)
            },
        }, class InterfacePropObject extends GObject.Object {});
        let file = Gio.File.new_for_path('dummy');
        expect(() => new InterfacePropObject({ file: file })).not.toThrow();
    });

    it('can override a property from the parent class', function () {
        const OverrideObject = GObject.registerClass({
            Properties: {
                'readwrite': GObject.ParamSpec.override('readwrite', MyObject),
            },
        }, class OverrideObject extends MyObject {
            get readwrite() {
                return this._subclass_readwrite;
            }
            set readwrite(val) {
                this._subclass_readwrite = 'subclass' + val;
            }
        });
        let obj = new OverrideObject();
        obj.readwrite = 'foo';
        expect(obj.readwrite).toEqual('subclassfoo');
    });

    it('cannot override a non-existent property', function () {
        expect(() => GObject.registerClass({
            Properties: {
                'nonexistent': GObject.ParamSpec.override('nonexistent', GObject.Object),
            },
        }, class BadOverride extends GObject.Object {})).toThrow();
    });

    it('does not pollute the wrong prototype with GObject properties', function () {
        const MyCustomCharset = GObject.registerClass(class MyCustomCharset extends Gio.CharsetConverter {
            _init() {
                super._init();
                void this.from_charset;
            }
        });

        const MySecondCustomCharset = GObject.registerClass(class MySecondCustomCharset extends GObject.Object {
            _init() {
                super._init();
                this.from_charset = 'another value';
            }
        });

        expect (() => new MyCustomCharset() && new MySecondCustomCharset()).not.toThrow();
    });

    it('resolves properties from interfaces', function() {
        const mon = Gio.NetworkMonitor.get_default();
        expect(mon.network_available).toBeDefined();
        expect(mon.networkAvailable).toBeDefined();
        expect(mon['network-available']).toBeDefined();
    });
});

const MyObjectWithJSObjectProperty = GObject.registerClass({
    Properties: {
        'jsobj-prop': GObject.ParamSpec.jsobject('jsobj-prop', 'jsobj-prop', 'jsobj-prop',
            GObject.ParamFlags.CONSTRUCT | GObject.ParamFlags.READWRITE, ''),
    }
}, class MyObjectWithJSObjectProperty extends GObject.Object {
});

describe('GObject class with JSObject property', function () {
    it('assigns a valid JSObject on construct', function () {
        let date = new Date();
        let obj = new MyObjectWithJSObjectProperty({jsobj_prop: date});
        expect(obj.jsobj_prop).toEqual(date);
        expect(obj.jsobj_prop).not.toEqual(new Date(0));
        expect(() => obj.jsobj_prop.setFullYear(1985)).not.toThrow();
        expect(obj.jsobj_prop.getFullYear()).toEqual(1985);
    });

    it('Set null with an empty JSObject on construct', function () {
        expect(new MyObjectWithJSObjectProperty().jsobj_prop).toBeNull();
        expect(new MyObjectWithJSObjectProperty({}).jsobj_prop).toBeNull();
    });

    it('assigns a null JSObject on construct', function () {
        expect(new MyObjectWithJSObjectProperty({jsobj_prop: null}).jsobj_prop)
            .toBeNull();
    });

    it('assigns a JSObject Array on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: [1, 2, 3]}))
            .not.toThrow();
    });

    it('assigns a Function on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: () => {
            return true;
        }})).not.toThrow();
    });

    it('throws an error when using a boolean value on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: true}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using an int value on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: 1}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using a numeric value on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: Math.PI}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using a string value on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: 'string'}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using an undefined value on construct', function () {
        expect(() => new MyObjectWithJSObjectProperty({jsobj_prop: undefined})).toThrow();
    });
});

const MyObjectWithJSObjectSignals = GObject.registerClass({
    Signals: {
        'send-object': {param_types: [GObject.TYPE_JSOBJECT]},
        'send-many-objects': {
            param_types: [GObject.TYPE_JSOBJECT,
                GObject.TYPE_JSOBJECT,
                GObject.TYPE_JSOBJECT]
        },
        'get-object': {
            flags: GObject.SignalFlags.RUN_LAST,
            accumulator: GObject.AccumulatorType.FIRST_WINS,
            return_type: GObject.TYPE_JSOBJECT,
            param_types: [GObject.TYPE_JSOBJECT],
        },
    },
}, class MyObjectWithJSObjectSignals extends GObject.Object {
    emitObject(obj) {
        this.emit('send-object', obj);
    }
});

describe('GObject class with JSObject signals', function () {
    let myInstance;
    beforeEach(function () {
        myInstance = new MyObjectWithJSObjectSignals();
    });

    it('emits signal with null JSObject parameter', function () {
        let customSpy = jasmine.createSpy('sendObjectSpy');
        myInstance.connect('send-object', customSpy);
        myInstance.emitObject(null);
        expect(customSpy).toHaveBeenCalledWith(myInstance, null);
    });

    it('emits signal with JSObject parameter', function () {
        let customSpy = jasmine.createSpy('sendObjectSpy');
        myInstance.connect('send-object', customSpy);

        let obj = {
            foo: [1, 2, 3],
            sub: {a: {}, 'b': this},
            desc: 'test',
            date: new Date()
        };
        myInstance.emitObject(obj);
        expect(customSpy).toHaveBeenCalledWith(myInstance, obj);
    });

    it('emits signal with multiple JSObject parameters', function () {
        let customSpy = jasmine.createSpy('sendManyObjectsSpy');
        myInstance.connect('send-many-objects', customSpy);

        let obj = {
            foo: [9, 8, 7, 'a', 'b', 'c'],
            sub: {a: {}, 'b': this},
            desc: 'test',
            date: new RegExp('\\w+')
        };
        myInstance.emit('send-many-objects', obj, obj.foo, obj.sub);
        expect(customSpy).toHaveBeenCalledWith(myInstance, obj, obj.foo, obj.sub);
    });

    it('re-emits signal with same JSObject parameter', function () {
        const System = imports.system;
        let obj = {
            foo: [9, 8, 7, 'a', 'b', 'c'],
            sub: {a: {}, 'b': this},
            func: (arg) => {
                return {ret: [arg]};
            },
        };

        myInstance.connect('send-many-objects', (instance, func, args, foo) => {
            expect(instance).toEqual(myInstance);
            expect(System.addressOf(instance)).toEqual(System.addressOf(myInstance));
            expect(foo).toEqual(obj.foo);
            expect(System.addressOf(foo)).toEqual(System.addressOf(obj.foo));
            expect(func(args).ret[0]).toEqual(args);
        });
        myInstance.connect('send-object', (instance, param) => {
            expect(instance).toEqual(myInstance);
            expect(System.addressOf(instance)).toEqual(System.addressOf(myInstance));
            expect(param).toEqual(obj);
            expect(System.addressOf(param)).toEqual(System.addressOf(obj));
            expect(() => instance.emit('send-many-objects', param.func, param, param.foo))
                .not.toThrow();
        });

        myInstance.emit('send-object', obj);
    });

    it('throws an error when using a boolean value as parameter', function () {
        expect(() => myInstance.emit('send-object', true))
            .toThrowError(/JSObject expected/);
        expect(() => myInstance.emit('send-many-objects', ['a'], true, {}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using an int value as parameter', function () {
        expect(() => myInstance.emit('send-object', 1))
            .toThrowError(/JSObject expected/);
        expect(() => myInstance.emit('send-many-objects', ['a'], 1, {}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using a numeric value as parameter', function () {
        expect(() => myInstance.emit('send-object', Math.PI))
            .toThrowError(/JSObject expected/);
        expect(() => myInstance.emit('send-many-objects', ['a'], Math.PI, {}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using a string value as parameter', function () {
        expect(() => myInstance.emit('send-object', 'string'))
            .toThrowError(/JSObject expected/);
        expect(() => myInstance.emit('send-many-objects', ['a'], 'string', {}))
            .toThrowError(/JSObject expected/);
    });

    it('throws an error when using an undefined value as parameter', function () {
        expect(() => myInstance.emit('send-object', undefined))
            .toThrowError(/JSObject expected/);
        expect(() => myInstance.emit('send-many-objects', ['a'], undefined, {}))
            .toThrowError(/JSObject expected/);
    });

    it('returns a JSObject', function () {
        let data = {
            foo: [9, 8, 7, 'a', 'b', 'c'],
            sub: {a: {}, 'b': this},
            func: (arg) => {
                return {ret: [arg]};
            },
        };
        let id = myInstance.connect('get-object', () => {
            return data;
        });
        expect(myInstance.emit('get-object', {})).toBe(data);
        myInstance.disconnect(id);

        myInstance.connect('get-object', (instance, input) => {
            if (input) {
                if (typeof input === 'function')
                    input();
                return input;
            }

            class SubObject {
                constructor() {
                    this.pi = Math.PI;
                }
                method() {}
                gobject() {
                    return GObject.Object;
                }
                get data() {
                    return data;
                }
            }

            return new SubObject();
        });

        expect(myInstance.emit('get-object', null).constructor.name).toBe('SubObject');
        expect(myInstance.emit('get-object', null).data).toBe(data);
        expect(myInstance.emit('get-object', null).pi).toBe(Math.PI);
        expect(() => myInstance.emit('get-object', null).method()).not.toThrow();
        expect(myInstance.emit('get-object', null).gobject()).toBe(GObject.Object);
        expect(new (myInstance.emit('get-object', null).gobject()) instanceof GObject.Object)
            .toBeTruthy();
        expect(myInstance.emit('get-object', data)).toBe(data);
        expect(myInstance.emit('get-object', jasmine.createSpy('callMeSpy')))
            .toHaveBeenCalled();
    });

    it('returns null when returning undefined', function () {
        myInstance.connect('get-object', () => {
            return undefined;
        });
        expect(myInstance.emit('get-object', {})).toBeNull();
    });

    it('returns null when not returning', function () {
        myInstance.connect('get-object', () => { });
        expect(myInstance.emit('get-object', {})).toBeNull();
    });

    // These tests really throws an error, but I can't find a way to catch it
    //
    // it('throws an error when returning a boolean value', function () {
    //     myInstance.connect('get-object', (instance, obj) => { return true; });
    //     expect(() => myInstance.emit('get-object', {}))
    //         .toThrowError(/JSObject expected/);
    // });

    // it('throws an error when returning an int value', function () {
    //     myInstance.connect('get-object', (instance, obj) => { return 1; });
    //     expect(() => myInstance.emit('get-object', {}))
    //         .toThrowError(/JSObject expected/);
    // });

    // it('throws an error when returning a numeric value', function () {
    //     myInstance.connect('get-object', (instance, obj) => { return Math.PI; });
    //     expect(() => myInstance.emit('get-object', {}))
    //         .toThrowError(/JSObject expected/);
    // });

    // it('throws an error when returning a string value', function () {
    //     myInstance.connect('get-object', (instance, obj) => { return 'string'; });
    //     expect(() => myInstance.emit('get-object', {}))
    //         .toThrowError(/JSObject expected/);
    // });
});

