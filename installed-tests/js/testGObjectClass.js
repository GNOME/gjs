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
        'minimal': {param_types: [GObject.TYPE_INT, GObject.TYPE_INT]},
        'full': {
            flags: GObject.SignalFlags.RUN_LAST,
            accumulator: GObject.AccumulatorType.FIRST_WINS,
            return_type: GObject.TYPE_INT,
            param_types: [],
        },
        'run-last': {flags: GObject.SignalFlags.RUN_LAST},
        'detailed': {
            flags: GObject.SignalFlags.RUN_FIRST | GObject.SignalFlags.DETAILED,
            param_types: [GObject.TYPE_STRING],
        },
    },
}, class MyObject extends GObject.Object {
    get readwrite() {
        if (typeof this._readwrite === 'undefined')
            return 'foo';
        return this._readwrite;
    }

    set readwrite(val) {
        if (val === 'ignore')
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

    notifyProp() {
        this._readonly = 'changed';

        this.notify('readonly');
    }

    emitEmpty() {
        this.emit('empty');
    }

    emitMinimal(one, two) {
        this.emit('minimal', one, two);
    }

    emitFull() {
        return this.emit('full');
    }

    emitDetailed() {
        this.emit('detailed::one');
        this.emit('detailed::two');
    }

    emitRunLast(callback) {
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
    GTypeFlags: GObject.TypeFlags.ABSTRACT,
}, class MyAbstractObject extends GObject.Object {
});

const MyApplication = GObject.registerClass({
    Signals: {'custom': {param_types: [GObject.TYPE_INT]}},
}, class MyApplication extends Gio.Application {
    emitCustom(n) {
        this.emit('custom', n);
    }
});

const MyInitable = GObject.registerClass({
    Implements: [Gio.Initable],
}, class MyInitable extends GObject.Object {
    vfunc_init(cancellable) {
        if (!(cancellable instanceof Gio.Cancellable))
            throw new Error('Bad argument');

        this.inited = true;
    }
});

const Derived = GObject.registerClass(class Derived extends MyObject {
    _init() {
        super._init({readwrite: 'yes'});
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
        expect(() => GObject.registerClass(class Bar extends Foo {})).toThrow();
    });

    it('throws an error when used with an abstract class', function () {
        expect(() => new MyAbstractObject()).toThrow();
    });

    it('constructs with default values for properties', function () {
        expect(myInstance.readwrite).toEqual('foo');
        expect(myInstance.readonly).toEqual('bar');
        expect(myInstance.construct).toEqual('default');
    });

    it('constructs with a hash of property values', function () {
        let myInstance2 = new MyObject({readwrite: 'baz', construct: 'asdf'});
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

        myInstance.notifyProp();
        myInstance.notifyProp();

        expect(notifySpy).toHaveBeenCalledTimes(2);
    });

    it('can define its own signals', function () {
        let emptySpy = jasmine.createSpy('emptySpy');
        myInstance.connect('empty', emptySpy);
        myInstance.emitEmpty();

        expect(emptySpy).toHaveBeenCalled();
        expect(myInstance.empty_called).toBeTruthy();
    });

    it('passes emitted arguments to signal handlers', function () {
        let minimalSpy = jasmine.createSpy('minimalSpy');
        myInstance.connect('minimal', minimalSpy);
        myInstance.emitMinimal(7, 5);

        expect(minimalSpy).toHaveBeenCalledWith(myInstance, 7, 5);
    });

    it('can return values from signals', function () {
        let fullSpy = jasmine.createSpy('fullSpy').and.returnValue(42);
        myInstance.connect('full', fullSpy);
        let result = myInstance.emitFull();

        expect(fullSpy).toHaveBeenCalled();
        expect(result).toEqual(42);
    });

    it('does not call first-wins signal handlers after one returns a value', function () {
        let neverCalledSpy = jasmine.createSpy('neverCalledSpy');
        myInstance.connect('full', () => 42);
        myInstance.connect('full', neverCalledSpy);
        myInstance.emitFull();

        expect(neverCalledSpy).not.toHaveBeenCalled();
        expect(myInstance.full_default_handler_called).toBeFalsy();
    });

    it('gets the return value of the default handler', function () {
        let result = myInstance.emitFull();

        expect(myInstance.full_default_handler_called).toBeTruthy();
        expect(result).toEqual(79);
    });

    it('calls run-last default handler last', function () {
        let stack = [];
        let runLastSpy = jasmine.createSpy('runLastSpy')
            .and.callFake(() => {
                stack.push(1);
            });
        myInstance.connect('run-last', runLastSpy);
        myInstance.emitRunLast(() => {
            stack.push(2);
        });

        expect(stack).toEqual([1, 2]);
    });

    it("can inherit from something that's not GObject.Object", function () {
        // ...and still get all the goodies of GObject.Class
        let instance = new MyApplication({application_id: 'org.gjs.Application'});
        let customSpy = jasmine.createSpy('customSpy');
        instance.connect('custom', customSpy);

        instance.emitCustom(73);
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
                    Gio.File.$gtype),
            },
        }, class InterfacePropObject extends GObject.Object {});
        let file = Gio.File.new_for_path('dummy');
        expect(() => new InterfacePropObject({file})).not.toThrow();
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
                this._subclass_readwrite = `subclass${val}`;
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

        expect(() => new MyCustomCharset() && new MySecondCustomCharset()).not.toThrow();
    });

    it('resolves properties from interfaces', function () {
        const mon = Gio.NetworkMonitor.get_default();
        expect(mon.network_available).toBeDefined();
        expect(mon.networkAvailable).toBeDefined();
        expect(mon['network-available']).toBeDefined();
    });

    it('has a toString() defintion', function () {
        expect(myInstance.toString()).toMatch(
            /\[object instance wrapper GType:Gjs_MyObject jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
        expect(new Derived().toString()).toMatch(
            /\[object instance wrapper GType:Gjs_Derived jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });
});

describe('GObject virtual function', function () {
    it('can have its property read', function () {
        expect(GObject.Object.prototype.vfunc_constructed).toBeTruthy();
    });

    it('can have its property overridden with an anonymous function', function () {
        let callback;

        let key = 'vfunc_constructed';

        class _SimpleTestClass1 extends GObject.Object {}

        if (GObject.Object.prototype.vfunc_constructed) {
            let parentFunc = GObject.Object.prototype.vfunc_constructed;
            _SimpleTestClass1.prototype[key] = function (...args) {
                parentFunc.call(this, ...args);
                callback('123');
            };
        } else {
            _SimpleTestClass1.prototype[key] = function () {
                callback('abc');
            };
        }

        callback = jasmine.createSpy('callback');

        const SimpleTestClass1 = GObject.registerClass({GTypeName: 'SimpleTestClass1'}, _SimpleTestClass1);
        new SimpleTestClass1();

        expect(callback).toHaveBeenCalledWith('123');
    });

    it('can access the parent prototype with super()', function () {
        let callback;

        class _SimpleTestClass2 extends GObject.Object {
            vfunc_constructed() {
                super.vfunc_constructed();
                callback('vfunc_constructed');
            }
        }

        callback = jasmine.createSpy('callback');

        const SimpleTestClass2 = GObject.registerClass({GTypeName: 'SimpleTestClass2'}, _SimpleTestClass2);
        new SimpleTestClass2();

        expect(callback).toHaveBeenCalledWith('vfunc_constructed');
    });

    it('handles non-existing properties', function () {
        const _SimpleTestClass3 = class extends GObject.Object {};

        _SimpleTestClass3.prototype.vfunc_doesnt_exist = function () {};

        if (GObject.Object.prototype.vfunc_doesnt_exist)
            fail('Virtual function should not exist');


        expect(() => GObject.registerClass({GTypeName: 'SimpleTestClass3'}, _SimpleTestClass3)).toThrow();
    });
});

describe('GObject creation using invalid base classes', () => {
    it('fails when trying to instantiate a class that inherits from a GObject type', function () {
        const BadInheritance = class extends GObject.Object {};
        const BadDerivedInheritance = class extends Derived {};

        expect(() => new BadInheritance()).toThrowError(/Tried to construct an object without a GType/);
        expect(() => new BadDerivedInheritance()).toThrowError(/Tried to construct an object without a GType/);
    });

    it('fails when trying to register a GObject class that inherits from a non-GObject type', function () {
        const BadInheritance = class extends GObject.Object {};
        expect(() => GObject.registerClass(class BadInheritanceDerived extends BadInheritance {}))
            .toThrowError(/Object 0x[a-f0-9]+ is not a subclass of GObject_Object, it's a Object/);
    });
});
