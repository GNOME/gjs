// -*- mode: js; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Giovanni Campagna <gcampagna@src.gnome.org>

imports.gi.versions.Gtk = '3.0';

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
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

    it('warns if more than one argument passed to the default constructor', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            '*Too many arguments*');

        new MyObject({readwrite: 'baz'}, 'this is ignored', 123);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'testGObjectClassTooManyArguments');
    });

    it('throws an error if the first argument to the default constructor is not a property hash', function () {
        expect(() => new MyObject('this is wrong')).toThrow();
    });

    it('accepts a property hash that is not a plain object', function () {
        expect(() => new MyObject(new GObject.Object())).not.toThrow();
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

    it('does not allow changing CONSTRUCT_ONLY properties', function () {
        myInstance.construct = 'val';
        expect(myInstance.construct).toEqual('default');
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

    it('gracefully bails out when overriding an unsupported vfunc type', function () {
        expect(() => GObject.registerClass({
            Implements: [Gio.AsyncInitable],
        }, class Foo extends GObject.Object {
            vfunc_init_async() {}
        })).toThrow();
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

describe('Auto accessor generation', function () {
    const AutoAccessors = GObject.registerClass({
        Properties: {
            'simple': GObject.ParamSpec.int('simple', 'Simple', 'Short-named property',
                GObject.ParamFlags.READWRITE, 0, 100, 24),
            'long-long-name': GObject.ParamSpec.int('long-long-name', 'Long long name',
                'Long-named property', GObject.ParamFlags.READWRITE, 0, 100, 48),
            'construct': GObject.ParamSpec.int('construct', 'Construct', 'Construct',
                GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT, 0, 100, 96),
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

        get ['kebab-name']() {
            this._kebabNameGetterCalled++;
            return 42;
        }

        set ['kebab-name'](value) {
            this._kebabNameSetterCalled++;
        }

        set missing_getter(value) {
            this._missingGetter = value;
        }

        get missing_setter() {
            return 42;
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
        expect(a['long-long-name']).toEqual(48);
        expect(a.construct).toEqual(96);
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
