// -*- mode: js; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Giovanni Campagna <gcampagna@src.gnome.org>

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=3.0';
import System from 'system';

imports.searchPath.unshift('resource:///org/gjs/jsunit/modules');
const {setPropertyInSloppyMode} = imports.sloppy;

// Sometimes tests pass if we are comparing two inaccurate values in JS with
// each other. That's fine for now. Then we just have to suppress the warnings.
function warn64(func, ...args) {
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
        '*cannot be safely stored*');
    const ret = func(...args);
    const error = new Error();
    GLib.test_assert_expected_messages_internal('Gjs',
        error.fileName, error.lineNumber, 'warn64');
    return ret;
}

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
        this._constructProp = val;
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

const MyObjectWithCustomConstructor = GObject.registerClass({
    Properties: {
        'readwrite': GObject.ParamSpec.string('readwrite', 'ParamReadwrite',
            'A read write parameter', GObject.ParamFlags.READWRITE, ''),
        'readonly': GObject.ParamSpec.string('readonly', 'ParamReadonly',
            'A readonly parameter', GObject.ParamFlags.READABLE, ''),
        'construct': GObject.ParamSpec.string('construct', 'ParamConstructOnly',
            'A readwrite construct-only parameter',
            GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
            ''),
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
}, class MyObjectWithCustomConstructor extends GObject.Object {
    _readwrite;
    _readonly;
    _constructProp;

    constructor({readwrite = 'foo', readonly = 'bar', construct = 'default'} = {}) {
        super();

        this._constructProp = construct;
        this._readwrite = readwrite;
        this._readonly = readonly;
    }

    get readwrite() {
        return this._readwrite;
    }

    set readwrite(val) {
        if (val === 'ignore')
            return;

        this._readwrite = val;
    }

    get readonly() {
        return this._readonly;
    }

    set readonly(val) {
        // this should never be called
        void val;
        this._readonly = 'bogus';
    }

    get construct() {
        return this._constructProp;
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

const MyFinalObject = GObject.registerClass({
    GTypeFlags: GObject.TypeFlags.FINAL,
}, class extends GObject.Object {
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

const DerivedWithCustomConstructor = GObject.registerClass(class DerivedWithCustomConstructor extends MyObjectWithCustomConstructor {
    constructor() {
        super({readwrite: 'yes'});
    }
});

const ObjectWithDefaultConstructor = GObject.registerClass(class ObjectWithDefaultConstructor extends GObject.Object {});

const Cla$$ = GObject.registerClass(class Cla$$ extends MyObject {});

const MyCustomInit = GObject.registerClass(class MyCustomInit extends GObject.Object {
    _instance_init() {
        this.foo = true;
    }
});

const NoName = GObject.registerClass(class extends GObject.Object {});

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

    it('throws if final class is inherited from', function () {
        try {
            GObject.registerClass(class extends MyFinalObject {});
            fail();
        } catch (e) {
            expect(e.message).toEqual('Cannot inherit from a final type');
        }
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

    it('warns if more than one argument passed to the default constructor', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            '*Too many arguments*');

        new ObjectWithDefaultConstructor({}, 'this is ignored', 123);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'testGObjectClassTooManyArguments');
    });

    it('throws an error if the first argument to the default constructor is not a property hash', function () {
        expect(() => new MyObject('this is wrong')).toThrow();
    });

    it('does not accept a property hash that is not a plain object', function () {
        expect(() => new MyObject(new GObject.Object())).toThrow();
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

    it('does not allow changing CONSTRUCT_ONLY properties in sloppy mode', function () {
        setPropertyInSloppyMode(myInstance, 'construct', 'val');
        expect(myInstance.construct).toEqual('default');
    });

    it('throws when setting CONSTRUCT_ONLY properties in strict mode', function () {
        expect(() => (myInstance.construct = 'val')).toThrow();
    });

    it('has a name', function () {
        expect(MyObject.name).toEqual('MyObject');
    });

    // the following would (should) cause a CRITICAL:
    // myInstance.readonly = 'val';

    it('has a notify signal', function () {
        let notifySpy = jasmine.createSpy('notifySpy');
        myInstance.connect('notify::readonly', notifySpy);

        myInstance.notifyProp();
        myInstance.notifyProp();

        expect(notifySpy).toHaveBeenCalledTimes(2);
    });

    function asyncIdle() {
        return new Promise(resolve => {
            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                resolve();
                return GLib.SOURCE_REMOVE;
            });
        });
    }

    it('disconnects connect_object signals on destruction', async function () {
        let callback = jasmine.createSpy('callback');
        callback.myInstance = myInstance;
        const instance2 = new MyObject();
        instance2.connect_object('empty', callback, myInstance, 0);

        instance2.emitEmpty();
        instance2.emitEmpty();

        expect(callback).toHaveBeenCalledTimes(2);

        const weakRef = new WeakRef(myInstance);
        myInstance = null;
        callback = null;

        await asyncIdle();
        System.gc();

        expect(weakRef.deref()).toBeUndefined();
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

    it('handles anonymous class expressions', function () {
        const obj = new NoName();
        expect(obj instanceof NoName).toBeTruthy();

        const NoName2 = GObject.registerClass(class extends GObject.Object {});
        const obj2 = new NoName2();
        expect(obj2 instanceof NoName2).toBeTruthy();
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

    it('can have an int64 property', function () {
        const PropInt64 = GObject.registerClass({
            Properties: {
                'int64': GObject.ParamSpec.int64('int64', 'int64', 'int64',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    GLib.MININT64_BIGINT, GLib.MAXINT64_BIGINT, 0),
            },
        }, class PropInt64 extends GObject.Object {});

        let int64 = GLib.MAXINT64_BIGINT - 5n;
        let obj = warn64(() => new PropInt64({int64}));
        expect(obj.int64).toEqual(Number(int64));

        int64 = GLib.MININT64_BIGINT + 555n;
        obj = warn64(() => new PropInt64({int64}));
        expect(obj.int64).toEqual(Number(int64));
    });

    it('can have a default int64 property', function () {
        const defaultValue = GLib.MAXINT64_BIGINT - 1000n;
        const PropInt64Init = GObject.registerClass({
            Properties: {
                'int64': GObject.ParamSpec.int64('int64', 'int64', 'int64',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    GLib.MININT64_BIGINT, GLib.MAXINT64_BIGINT,
                    defaultValue),
            },
        }, class PropDefaultInt64Init extends GObject.Object {});

        const obj = warn64(() => new PropInt64Init());
        expect(obj.int64).toEqual(Number(defaultValue));
    });

    it('can have an uint64 property', function () {
        const PropUint64 = GObject.registerClass({
            Properties: {
                'uint64': GObject.ParamSpec.uint64('uint64', 'uint64', 'uint64',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    0, GLib.MAXUINT64_BIGINT, 0),
            },
        }, class PropUint64 extends GObject.Object {});

        const uint64 = GLib.MAXUINT64_BIGINT - 5n;
        const obj = warn64(() => new PropUint64({uint64}));
        expect(obj.uint64).toEqual(Number(uint64));
    });

    it('can have a default uint64 property', function () {
        const defaultValue = GLib.MAXUINT64_BIGINT;
        const PropUint64Init = GObject.registerClass({
            Properties: {
                'uint64': GObject.ParamSpec.uint64('uint64', 'uint64', 'uint64',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    0n, GLib.MAXUINT64_BIGINT, defaultValue),
            },
        }, class PropDefaultUint64Init extends GObject.Object {});

        const obj = warn64(() => new PropUint64Init());
        expect(obj.uint64).toEqual(Number(defaultValue));
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

    it('handles gracefully forgetting to override a C property', function () {
        GLib.test_expect_message('GLib-GObject', GLib.LogLevelFlags.LEVEL_CRITICAL,
            "*Object class Gjs_ForgottenOverride doesn't implement property " +
            "'anchors' from interface 'GTlsFileDatabase'*");

        // This is a random interface in Gio with a read-write property
        const ForgottenOverride = GObject.registerClass({
            Implements: [Gio.TlsFileDatabase],
        }, class ForgottenOverride extends Gio.TlsDatabase {});
        const obj = new ForgottenOverride();
        expect(obj.anchors).not.toBeDefined();
        expect(() => (obj.anchors = 'foo')).not.toThrow();
        expect(obj.anchors).toEqual('foo');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'testGObjectClassForgottenOverride');
    });

    it('handles gracefully overriding a C property but forgetting the accessors', function () {
        // This is a random interface in Gio with a read-write property
        const ForgottenAccessors = GObject.registerClass({
            Implements: [Gio.TlsFileDatabase],
            Properties: {
                'anchors': GObject.ParamSpec.override('anchors', Gio.TlsFileDatabase),
            },
        }, class ForgottenAccessors extends Gio.TlsDatabase {});
        const obj = new ForgottenAccessors();
        expect(obj.anchors).toBeNull();  // the property's default value
        obj.anchors = 'foo';
        expect(obj.anchors).toEqual('foo');

        const ForgottenAccessors2 =
            GObject.registerClass(class ForgottenAccessors2 extends ForgottenAccessors {});
        const obj2 = new ForgottenAccessors2();
        expect(obj2.anchors).toBeNull();
        obj2.anchors = 'foo';
        expect(obj2.anchors).toEqual('foo');
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

        expect(() => {
            new MyCustomCharset();
            new MySecondCustomCharset();
        }).not.toThrow();
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

    it('does not clobber native parent interface vfunc definitions', function () {
        const resetImplementationSpy = jasmine.createSpy('vfunc_reset');
        expect(() => {
            // This is a random interface in Gio with a virtual function
            GObject.registerClass({
                // Forgotten interface
                // Implements: [Gio.Converter],
            }, class MyZlibConverter extends Gio.ZlibCompressor {
                vfunc_reset() {
                    resetImplementationSpy();
                }
            });
        }).toThrowError('Gjs_MyZlibConverter does not implement Gio.Converter, add Gio.Converter to your implements array');

        let potentiallyClobbered = new Gio.ZlibCompressor();
        potentiallyClobbered.reset();

        expect(resetImplementationSpy).not.toHaveBeenCalled();
    });

    it('does not clobber dynamic parent interface vfunc definitions', function () {
        const resetImplementationSpy = jasmine.createSpy('vfunc_reset');

        const MyJSConverter = GObject.registerClass({
            Implements: [Gio.Converter],
        }, class MyJSConverter extends GObject.Object {
            vfunc_reset() {
            }
        });

        expect(() => {
            GObject.registerClass({
                // Forgotten interface
                // Implements: [Gio.Converter],
            }, class MyBadConverter extends MyJSConverter {
                vfunc_reset() {
                    resetImplementationSpy();
                }
            });
        }).toThrowError('Gjs_MyBadConverter does not implement Gio.Converter, add Gio.Converter to your implements array');

        let potentiallyClobbered = new MyJSConverter();
        potentiallyClobbered.reset();

        expect(resetImplementationSpy).not.toHaveBeenCalled();
    });
});

describe('GObject class with custom constructor', function () {
    let myInstance;
    beforeEach(function () {
        myInstance = new MyObjectWithCustomConstructor();
    });

    it('throws an error when not used with a GObject-derived class', function () {
        class Foo {}
        expect(() => GObject.registerClass(class Bar extends Foo {})).toThrow();
    });

    it('constructs with default values for properties', function () {
        expect(myInstance.readwrite).toEqual('foo');
        expect(myInstance.readonly).toEqual('bar');
        expect(myInstance.construct).toEqual('default');
    });

    it('has a toString() defintion', function () {
        expect(myInstance.toString()).toMatch(
            /\[object instance wrapper GType:Gjs_MyObjectWithCustomConstructor jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });

    it('constructs with a hash of property values', function () {
        let myInstance2 = new MyObjectWithCustomConstructor({readwrite: 'baz', construct: 'asdf'});
        expect(myInstance2.readwrite).toEqual('baz');
        expect(myInstance2.readonly).toEqual('bar');
        console.log(Object.getOwnPropertyDescriptor(myInstance2, 'construct'));
        expect(myInstance2.construct).toEqual('asdf');
    });

    it('accepts a property hash that is not a plain object', function () {
        expect(() => new MyObjectWithCustomConstructor(new GObject.Object())).not.toThrow();
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

    it('does not allow changing CONSTRUCT_ONLY properties in sloppy mode', function () {
        setPropertyInSloppyMode(myInstance, 'construct', 'val');
        expect(myInstance.construct).toEqual('default');
    });

    it('does not allow changing CONSTRUCT_ONLY properties in strict mode', function () {
        expect(() => (myInstance.construct = 'val')).toThrow();
    });

    it('has a name', function () {
        expect(MyObjectWithCustomConstructor.name).toEqual('MyObjectWithCustomConstructor');
    });

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

    it('can be a subclass', function () {
        let derived = new DerivedWithCustomConstructor();

        expect(derived instanceof DerivedWithCustomConstructor).toBeTruthy();
        expect(derived instanceof MyObjectWithCustomConstructor).toBeTruthy();

        expect(derived.readwrite).toEqual('yes');
    });

    it('can override a property from the parent class', function () {
        const OverrideObjectWithCustomConstructor = GObject.registerClass({
            Properties: {
                'readwrite': GObject.ParamSpec.override('readwrite', MyObjectWithCustomConstructor),
            },
        }, class OverrideObjectWithCustomConstructor extends MyObjectWithCustomConstructor {
            get readwrite() {
                return this._subclass_readwrite;
            }

            set readwrite(val) {
                this._subclass_readwrite = `subclass${val}`;
            }
        });
        let obj = new OverrideObjectWithCustomConstructor();
        obj.readwrite = 'foo';
        expect(obj.readwrite).toEqual('subclassfoo');
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

    it('gracefully bails out when overriding an unsupported vfunc type', function () {
        expect(() => GObject.registerClass({
            Implements: [Gio.AsyncInitable],
        }, class Foo extends GObject.Object {
            vfunc_init_async() {}
        })).toThrow();

        expect(() => GObject.registerClass({
            Implements: [Gio.AsyncInitable],
        }, class FooStatic extends GObject.Object {
            static vfunc_not_existing() {}
        })).toThrow();
    });

    it('are defined also for static virtual functions', function () {
        const CustomEmptyGIcon = GObject.registerClass({
            Implements: [Gio.Icon],
        }, class CustomEmptyGIcon extends GObject.Object {});
        expect(Gio.Icon.deserialize).toBeInstanceOf(Function);
        expect(CustomEmptyGIcon.deserialize).toBe(Gio.Icon.deserialize);
        expect(Gio.Icon.new_for_string).toBeInstanceOf(Function);
        expect(CustomEmptyGIcon.new_for_string).toBe(Gio.Icon.new_for_string);
    });

    it('supports static methods', function () {
        if (!Gio.Icon.vfunc_from_tokens)
            pending('https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/543');
        expect(() => GObject.registerClass({
            Implements: [Gio.Icon],
        }, class extends GObject.Object {
            static vfunc_from_tokens() {}
        })).not.toThrow();
    });

    it('must be non-static for methods', function () {
        expect(() => GObject.registerClass({
            Implements: [Gio.Icon],
        }, class extends GObject.Object {
            static vfunc_serialize() {}
        })).toThrowError(/.* static definition of non-static.*/);
    });

    it('must be static for methods', function () {
        if (!Gio.Icon.vfunc_from_tokens)
            pending('https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/543');
        expect(() => GObject.registerClass({
            Implements: [Gio.Icon],
        }, class extends GObject.Object {
            vfunc_from_tokens() {}
        })).toThrowError(/.*non-static definition of static.*/);
    });
});

describe('GObject creation using base classes without registered GType', function () {
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

describe('Register GType name', function () {
    beforeAll(function () {
        expect(GObject.gtypeNameBasedOnJSPath).toBeFalsy();
    });

    afterEach(function () {
        GObject.gtypeNameBasedOnJSPath = false;
    });

    it('uses the class name', function () {
        const GTypeTestAutoName = GObject.registerClass(
            class GTypeTestAutoName extends GObject.Object { });

        expect(GTypeTestAutoName.$gtype.name).toEqual(
            'Gjs_GTypeTestAutoName');
    });

    it('uses the sanitized class name', function () {
        const GTypeTestAutoName = GObject.registerClass(
            class GTypeTestAutoCla$$Name extends GObject.Object { });

        expect(GTypeTestAutoName.$gtype.name).toEqual(
            'Gjs_GTypeTestAutoCla__Name');
    });

    it('use the file path and class name', function () {
        GObject.gtypeNameBasedOnJSPath = true;
        const GTypeTestAutoName = GObject.registerClass(
            class GTypeTestAutoName extends GObject.Object {});

        /* Update this test if the file is moved */
        expect(GTypeTestAutoName.$gtype.name).toEqual(
            'Gjs_js_testGObjectClass_GTypeTestAutoName');
    });

    it('use the file path and sanitized class name', function () {
        GObject.gtypeNameBasedOnJSPath = true;
        const GTypeTestAutoName = GObject.registerClass(
            class GTypeTestAutoCla$$Name extends GObject.Object { });

        /* Update this test if the file is moved */
        expect(GTypeTestAutoName.$gtype.name).toEqual(
            'Gjs_js_testGObjectClass_GTypeTestAutoCla__Name');
    });

    it('use provided class name', function () {
        const GtypeClass = GObject.registerClass({
            GTypeName: 'GTypeTestManualName',
        }, class extends GObject.Object {});

        expect(GtypeClass.$gtype.name).toEqual('GTypeTestManualName');
    });

    it('sanitizes user provided class name', function () {
        let gtypeName = 'GType$Test/WithLòt\'s of*bad§chars!';
        let expectedSanitized = 'GType_Test_WithL_t_s_of_bad_chars_';

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            `*RangeError: Provided GType name '${gtypeName}' is not valid; ` +
            `automatically sanitized to '${expectedSanitized}'*`);

        const GtypeClass = GObject.registerClass({
            GTypeName: gtypeName,
        }, class extends GObject.Object {});

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'testGObjectRegisterClassSanitize');

        expect(GtypeClass.$gtype.name).toEqual(expectedSanitized);
    });
});

describe('Signal handler matching', function () {
    let o, handleEmpty, emptyId, handleDetailed, detailedId, handleDetailedOne,
        detailedOneId, handleDetailedTwo, detailedTwoId, handleNotifyTwo,
        notifyTwoId, handleMinimalOrFull, minimalId, fullId;

    beforeEach(function () {
        o = new MyObject();
        handleEmpty = jasmine.createSpy('handleEmpty');
        emptyId = o.connect('empty', handleEmpty);
        handleDetailed = jasmine.createSpy('handleDetailed');
        detailedId = o.connect('detailed', handleDetailed);
        handleDetailedOne = jasmine.createSpy('handleDetailedOne');
        detailedOneId = o.connect('detailed::one', handleDetailedOne);
        handleDetailedTwo = jasmine.createSpy('handleDetailedTwo');
        detailedTwoId = o.connect('detailed::two', handleDetailedTwo);
        handleNotifyTwo = jasmine.createSpy('handleNotifyTwo');
        notifyTwoId = o.connect('notify::two', handleNotifyTwo);
        handleMinimalOrFull = jasmine.createSpy('handleMinimalOrFull');
        minimalId = o.connect('minimal', handleMinimalOrFull);
        fullId = o.connect('full', handleMinimalOrFull);
    });

    it('finds handlers by signal ID', function () {
        expect(GObject.signal_handler_find(o, {signalId: 'empty'})).toEqual(emptyId);
        // when more than one are connected, returns an arbitrary one
        expect([detailedId, detailedOneId, detailedTwoId])
            .toContain(GObject.signal_handler_find(o, {signalId: 'detailed'}));
    });

    it('finds handlers by signal detail', function () {
        expect(GObject.signal_handler_find(o, {detail: 'one'})).toEqual(detailedOneId);
        // when more than one are connected, returns an arbitrary one
        expect([detailedTwoId, notifyTwoId])
            .toContain(GObject.signal_handler_find(o, {detail: 'two'}));
    });

    it('finds handlers by callback', function () {
        expect(GObject.signal_handler_find(o, {func: handleEmpty})).toEqual(emptyId);
        expect(GObject.signal_handler_find(o, {func: handleDetailed})).toEqual(detailedId);
        expect(GObject.signal_handler_find(o, {func: handleDetailedOne})).toEqual(detailedOneId);
        expect(GObject.signal_handler_find(o, {func: handleDetailedTwo})).toEqual(detailedTwoId);
        expect(GObject.signal_handler_find(o, {func: handleNotifyTwo})).toEqual(notifyTwoId);
        // when more than one are connected, returns an arbitrary one
        expect([minimalId, fullId])
            .toContain(GObject.signal_handler_find(o, {func: handleMinimalOrFull}));
    });

    it('finds handlers by a combination of parameters', function () {
        expect(GObject.signal_handler_find(o, {signalId: 'detailed', detail: 'two'}))
            .toEqual(detailedTwoId);
        expect(GObject.signal_handler_find(o, {signalId: 'detailed', func: handleDetailed}))
            .toEqual(detailedId);
    });

    it('blocks a handler by callback', function () {
        expect(GObject.signal_handlers_block_matched(o, {func: handleEmpty})).toEqual(1);
        o.emitEmpty();
        expect(handleEmpty).not.toHaveBeenCalled();

        expect(GObject.signal_handlers_unblock_matched(o, {func: handleEmpty})).toEqual(1);
        o.emitEmpty();
        expect(handleEmpty).toHaveBeenCalled();
    });

    it('blocks multiple handlers by callback', function () {
        expect(GObject.signal_handlers_block_matched(o, {func: handleMinimalOrFull})).toEqual(2);
        o.emitMinimal();
        o.emitFull();
        expect(handleMinimalOrFull).not.toHaveBeenCalled();

        expect(GObject.signal_handlers_unblock_matched(o, {func: handleMinimalOrFull})).toEqual(2);
        o.emitMinimal();
        o.emitFull();
        expect(handleMinimalOrFull).toHaveBeenCalledTimes(2);
    });

    it('blocks handlers by a combination of parameters', function () {
        expect(GObject.signal_handlers_block_matched(o, {signalId: 'detailed', func: handleDetailed}))
            .toEqual(1);
        o.emit('detailed', '');
        o.emit('detailed::one', '');
        expect(handleDetailed).not.toHaveBeenCalled();
        expect(handleDetailedOne).toHaveBeenCalled();

        expect(GObject.signal_handlers_unblock_matched(o, {signalId: 'detailed', func: handleDetailed}))
            .toEqual(1);
        o.emit('detailed', '');
        o.emit('detailed::one', '');
        expect(handleDetailed).toHaveBeenCalled();
    });

    it('disconnects a handler by callback', function () {
        expect(GObject.signal_handlers_disconnect_matched(o, {func: handleEmpty})).toEqual(1);
        o.emitEmpty();
        expect(handleEmpty).not.toHaveBeenCalled();
    });

    it('blocks multiple handlers by callback', function () {
        expect(GObject.signal_handlers_disconnect_matched(o, {func: handleMinimalOrFull})).toEqual(2);
        o.emitMinimal();
        o.emitFull();
        expect(handleMinimalOrFull).not.toHaveBeenCalled();
    });

    it('blocks handlers by a combination of parameters', function () {
        expect(GObject.signal_handlers_disconnect_matched(o, {signalId: 'detailed', func: handleDetailed}))
            .toEqual(1);
        o.emit('detailed', '');
        o.emit('detailed::one', '');
        expect(handleDetailed).not.toHaveBeenCalled();
        expect(handleDetailedOne).toHaveBeenCalled();
    });

    it('blocks a handler by callback, convenience method', function () {
        expect(GObject.signal_handlers_block_by_func(o, handleEmpty)).toEqual(1);
        o.emitEmpty();
        expect(handleEmpty).not.toHaveBeenCalled();

        expect(GObject.signal_handlers_unblock_by_func(o, handleEmpty)).toEqual(1);
        o.emitEmpty();
        expect(handleEmpty).toHaveBeenCalled();
    });

    it('disconnects a handler by callback, convenience method', function () {
        expect(GObject.signal_handlers_disconnect_by_func(o, handleEmpty)).toEqual(1);
        o.emitEmpty();
        expect(handleEmpty).not.toHaveBeenCalled();
    });

    it('does not support disconnecting a handler by callback data', function () {
        expect(() => GObject.signal_handlers_disconnect_by_data(o, null)).toThrow();
    });
});

describe('Property bindings', function () {
    const ObjectWithProperties = GObject.registerClass({
        Properties: {
            'string': GObject.ParamSpec.string('string', 'String', 'String property',
                GObject.ParamFlags.READWRITE, ''),
            'bool': GObject.ParamSpec.boolean('bool', 'Bool', 'Bool property',
                GObject.ParamFlags.READWRITE, true),
        },
    }, class ObjectWithProperties extends GObject.Object {});

    let a, b;
    beforeEach(function () {
        a = new ObjectWithProperties();
        b = new ObjectWithProperties();
    });

    it('can bind properties of the same type', function () {
        a.bind_property('string', b, 'string', GObject.BindingFlags.NONE);
        a.string = 'foo';
        expect(a.string).toEqual('foo');
        expect(b.string).toEqual('foo');
    });

    it('can use custom mappings to bind properties of different types', function () {
        a.bind_property_full('bool', b, 'string', GObject.BindingFlags.NONE,
            (bind, source) => [true, `${source}`],
            null);
        a.bool = true;
        expect(a.bool).toEqual(true);
        expect(b.string).toEqual('true');
    });

    it('can be set up as a group', function () {
        const group = new GObject.BindingGroup({source: a});
        group.bind('string', b, 'string', GObject.BindingFlags.NONE);
        a.string = 'foo';
        expect(a.string).toEqual('foo');
        expect(b.string).toEqual('foo');
    });

    it('can be set up as a group with custom mappings', function () {
        const group = new GObject.BindingGroup({source: a});
        group.bind_full('bool', b, 'string', GObject.BindingFlags.NONE,
            (bind, source) => [true, `${source}`],
            null);
        a.bool = true;
        expect(a.bool).toEqual(true);
        expect(b.string).toEqual('true');
    });
});

describe('Auto accessor generation', function () {
    const AutoAccessors = GObject.registerClass({
        Properties: {
            'simple': GObject.ParamSpec.int('simple', 'Simple', 'Short-named property',
                GObject.ParamFlags.READWRITE, 0, 100, 24),
            'long-long-name': GObject.ParamSpec.int('long-long-name', 'Long long name',
                'Long-named property', GObject.ParamFlags.READWRITE, 0, 100, 48),
            'construct': GObject.ParamSpec.int('construct', 'Construct', 'Construct',
                GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT, 0, 100, 96),
            'construct-only': GObject.ParamSpec.int('construct-only', 'Construct only',
                'Construct-only property',
                GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                0, 100, 80),
            'construct-only-with-setter': GObject.ParamSpec.int('construct-only-with-setter', 'Construct only with setter',
                'Construct-only property with a setter method',
                GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                0, 100, 80),
            'construct-only-was-invalid-in-turkish': GObject.ParamSpec.int(
                'construct-only-was-invalid-in-turkish', 'Camel name in Turkish',
                'Camel-cased property that was wrongly transformed in Turkish',
                GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                0, 100, 55),
            'snake-name': GObject.ParamSpec.int('snake-name', 'Snake name',
                'Snake-cased property', GObject.ParamFlags.READWRITE, 0, 100, 36),
            'camel-name': GObject.ParamSpec.int('camel-name', 'Camel name',
                'Camel-cased property', GObject.ParamFlags.READWRITE, 0, 100, 72),
            'kebab-name': GObject.ParamSpec.int('kebab-name', 'Kebab name',
                'Kebab-cased property', GObject.ParamFlags.READWRITE, 0, 100, 12),
            'readonly': GObject.ParamSpec.int('readonly', 'Readonly', 'Readonly property',
                GObject.ParamFlags.READABLE, 0, 100, 54),
            'writeonly': GObject.ParamSpec.int('writeonly', 'Writeonly',
                'Writeonly property', GObject.ParamFlags.WRITABLE, 0, 100, 60),
            'missing-getter': GObject.ParamSpec.int('missing-getter', 'Missing getter',
                'Missing a getter', GObject.ParamFlags.READWRITE, 0, 100, 18),
            'missing-setter': GObject.ParamSpec.int('missing-setter', 'Missing setter',
                'Missing a setter', GObject.ParamFlags.READWRITE, 0, 100, 42),
        },
    }, class AutoAccessors extends GObject.Object {
        _init(props = {}) {
            this._constructOnlySetterCalled = 0;
            super._init(props);
            this._snakeNameGetterCalled = 0;
            this._snakeNameSetterCalled = 0;
            this._camelNameGetterCalled = 0;
            this._camelNameSetterCalled = 0;
            this._kebabNameGetterCalled = 0;
            this._kebabNameSetterCalled = 0;
        }

        get snake_name() {
            this._snakeNameGetterCalled++;
            return 42;
        }

        set snake_name(value) {
            this._snakeNameSetterCalled++;
        }

        get camelName() {
            this._camelNameGetterCalled++;
            return 42;
        }

        set camelName(value) {
            this._camelNameSetterCalled++;
        }

        get 'kebab-name'() {
            this._kebabNameGetterCalled++;
            return 42;
        }

        set 'kebab-name'(value) {
            this._kebabNameSetterCalled++;
        }

        set missing_getter(value) {
            this._missingGetter = value;
        }

        get missing_setter() {
            return 42;
        }

        get construct_only_with_setter() {
            return this._constructOnlyValue;
        }

        set constructOnlyWithSetter(value) {
            this._constructOnlySetterCalled++;
            this._constructOnlyValue = value;
        }
    });

    let a;
    beforeEach(function () {
        a = new AutoAccessors();
    });

    it('get and set the property', function () {
        a.simple = 1;
        expect(a.simple).toEqual(1);
        a['long-long-name'] = 1;
        expect(a['long-long-name']).toEqual(1);
        a.construct = 1;
        expect(a.construct).toEqual(1);
    });

    it("initial value is the param spec's default value", function () {
        expect(a.simple).toEqual(24);
        expect(a.long_long_name).toEqual(48);
        expect(a.longLongName).toEqual(48);
        expect(a['long-long-name']).toEqual(48);
        expect(a.construct).toEqual(96);
        expect(a.construct_only).toEqual(80);
        expect(a.constructOnly).toEqual(80);
        expect(a['construct-only']).toEqual(80);
    });

    it('set properties at construct time', function () {
        a = new AutoAccessors({
            simple: 1,
            longLongName: 1,
            construct: 1,
            'construct-only': 1,
            'construct-only-with-setter': 2,
        });
        expect(a.simple).toEqual(1);
        expect(a.long_long_name).toEqual(1);
        expect(a.longLongName).toEqual(1);
        expect(a['long-long-name']).toEqual(1);
        expect(a.construct).toEqual(1);
        expect(a.construct_only).toEqual(1);
        expect(a.constructOnly).toEqual(1);
        expect(a['construct-only']).toEqual(1);
        expect(a.constructOnlyWithSetter).toEqual(2);
        expect(a.construct_only_with_setter).toEqual(2);
        expect(a['construct-only-with-setter']).toEqual(2);
        expect(a._constructOnlySetterCalled).toEqual(1);
    });

    it('set properties at construct time with locale', function () {
        const {gettext: Gettext} = imports;
        const prevLocale = Gettext.setlocale(Gettext.LocaleCategory.ALL, null);

        Gettext.setlocale(Gettext.LocaleCategory.ALL, 'tr_TR');
        a = new AutoAccessors({
            'construct-only-was-invalid-in-turkish': 35,
        });
        Gettext.setlocale(Gettext.LocaleCategory.ALL, prevLocale);

        expect(a.constructOnlyWasInvalidInTurkish).toEqual(35);
        expect(a.construct_only_was_invalid_in_turkish).toEqual(35);
        expect(a['construct-only-was-invalid-in-turkish']).toEqual(35);
    });

    it('notify when the property changes', function () {
        const notify = jasmine.createSpy('notify');
        a.connect('notify::simple', notify);
        a.simple = 1;
        expect(notify).toHaveBeenCalledTimes(1);
        notify.calls.reset();
        a.simple = 1;
        expect(notify).not.toHaveBeenCalled();
    });

    it('copies accessors for camel and kebab if snake accessors given', function () {
        a.snakeName = 42;
        expect(a.snakeName).toEqual(42);
        a['snake-name'] = 42;
        expect(a['snake-name']).toEqual(42);
        expect(a._snakeNameGetterCalled).toEqual(2);
        expect(a._snakeNameSetterCalled).toEqual(2);
    });

    it('copies accessors for snake and kebab if camel accessors given', function () {
        a.camel_name = 42;
        expect(a.camel_name).toEqual(42);
        a['camel-name'] = 42;
        expect(a['camel-name']).toEqual(42);
        expect(a._camelNameGetterCalled).toEqual(2);
        expect(a._camelNameSetterCalled).toEqual(2);
    });

    it('copies accessors for snake and camel if kebab accessors given', function () {
        a.kebabName = 42;
        expect(a.kebabName).toEqual(42);
        a.kebab_name = 42;
        expect(a.kebab_name).toEqual(42);
        expect(a._kebabNameGetterCalled).toEqual(2);
        expect(a._kebabNameSetterCalled).toEqual(2);
    });

    it('readonly getter throws', function () {
        expect(() => a.readonly).toThrowError(/getter/);
    });

    it('writeonly setter throws', function () {
        expect(() => (a.writeonly = 1)).toThrowError(/setter/);
    });

    it('getter throws when setter defined', function () {
        expect(() => a.missingGetter).toThrowError(/getter/);
    });

    it('setter throws when getter defined', function () {
        expect(() => (a.missingSetter = 1)).toThrowError(/setter/);
    });
});

const MyObjectWithJSObjectProperty = GObject.registerClass({
    Properties: {
        'jsobj-prop': GObject.ParamSpec.jsobject('jsobj-prop', 'jsobj-prop', 'jsobj-prop',
            GObject.ParamFlags.CONSTRUCT | GObject.ParamFlags.READWRITE),
    },
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
        expect(() => new MyObjectWithJSObjectProperty({
            jsobj_prop: () => true,
        })).not.toThrow();
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

    it('property value survives when GObject wrapper is collected', function () {
        const MyConverter = GObject.registerClass({
            Properties: {
                testprop: GObject.ParamSpec.jsobject('testprop', 'testprop', 'Test property',
                    GObject.ParamFlags.CONSTRUCT | GObject.ParamFlags.READWRITE),
            },
            Implements: [Gio.Converter],
        }, class MyConverter extends GObject.Object {});

        function stashObject() {
            const base = new Gio.MemoryInputStream();
            const converter = new MyConverter({testprop: [1, 2, 3]});
            return Gio.ConverterInputStream.new(base, converter);
        }

        const stream = stashObject();
        System.gc();
        expect(stream.get_converter().testprop).toEqual([1, 2, 3]);
    });
});

const MyObjectWithJSObjectSignals = GObject.registerClass({
    Signals: {
        'send-object': {param_types: [GObject.TYPE_JSOBJECT]},
        'send-many-objects': {
            param_types: [GObject.TYPE_JSOBJECT,
                GObject.TYPE_JSOBJECT,
                GObject.TYPE_JSOBJECT],
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
            sub: {a: {}, 'b': globalThis},
            desc: 'test',
            date: new Date(),
        };
        myInstance.emitObject(obj);
        expect(customSpy).toHaveBeenCalledWith(myInstance, obj);
    });

    it('emits signal with multiple JSObject parameters', function () {
        let customSpy = jasmine.createSpy('sendManyObjectsSpy');
        myInstance.connect('send-many-objects', customSpy);

        let obj = {
            foo: [9, 8, 7, 'a', 'b', 'c'],
            sub: {a: {}, 'b': globalThis},
            desc: 'test',
            date: new RegExp('\\w+'),
        };
        myInstance.emit('send-many-objects', obj, obj.foo, obj.sub);
        expect(customSpy).toHaveBeenCalledWith(myInstance, obj, obj.foo, obj.sub);
    });

    it('re-emits signal with same JSObject parameter', function () {
        let obj = {
            foo: [9, 8, 7, 'a', 'b', 'c'],
            sub: {a: {}, 'b': globalThis},
            func: arg => {
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
            sub: {a: {}, 'b': globalThis},
            func: arg => {
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
        expect(new (myInstance.emit('get-object', null).gobject())() instanceof GObject.Object)
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

    // These tests are intended to throw an error, but currently errors cannot
    // be caught from signal handlers, so we check for logged messages instead

    it('throws an error when returning a boolean value', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*JSObject expected*');
        myInstance.connect('get-object', () => true);
        myInstance.emit('get-object', {});
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'throws an error when returning a boolean value');
    });

    it('throws an error when returning an int value', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*JSObject expected*');
        myInstance.connect('get-object', () => 1);
        myInstance.emit('get-object', {});
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'throws an error when returning a boolean value');
    });

    it('throws an error when returning a numeric value', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*JSObject expected*');
        myInstance.connect('get-object', () => Math.PI);
        myInstance.emit('get-object', {});
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'throws an error when returning a boolean value');
    });

    it('throws an error when returning a string value', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*JSObject expected*');
        myInstance.connect('get-object', () => 'string');
        myInstance.emit('get-object', {});
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'throws an error when returning a boolean value');
    });
});

describe('GObject class with int64 properties', function () {
    const MyInt64Class = GObject.registerClass(class MyInt64Class extends GObject.Object {
        static [GObject.properties] = {
            'int64': GObject.ParamSpec.int64('int64', 'int64', 'int64',
                GObject.ParamFlags.READABLE | GObject.ParamFlags.WRITABLE | GObject.ParamFlags.CONSTRUCT,
                // GLib.MAXINT64 exceeds JS' ability to safely represent an integer
                GLib.MININT32 * 2, GLib.MAXINT32 * 2, 0),
        };
    });

    it('can set an int64 property', function () {
        const instance = new MyInt64Class({
            int64: GLib.MAXINT32,
        });

        expect(instance.int64).toBe(GLib.MAXINT32);

        instance.int64 = GLib.MAXINT32 + 1;

        expect(instance.int64).toBe(GLib.MAXINT32 + 1);
    });


    it('can construct with int64 property', function () {
        const instance = new MyInt64Class({
            int64: GLib.MAXINT32 + 1,
        });

        expect(instance.int64).toBe(GLib.MAXINT32 + 1);
    });
});
