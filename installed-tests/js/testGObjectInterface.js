// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2015 Endless Mobile, Inc.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';

const AGObjectInterface = GObject.registerClass({
    GTypeName: 'ArbitraryGTypeName',
    Requires: [GObject.Object],
    Properties: {
        'interface-prop': GObject.ParamSpec.string('interface-prop',
            'Interface property', 'Must be overridden in implementation',
            GObject.ParamFlags.READABLE,
            'foobar'),
    },
    Signals: {
        'interface-signal': {},
    },
}, class AGObjectInterface extends GObject.Interface {
    requiredG() {
        throw new GObject.NotImplementedError();
    }

    optionalG() {
        return 'AGObjectInterface.optionalG()';
    }
});

const InterfaceRequiringGObjectInterface = GObject.registerClass({
    Requires: [AGObjectInterface],
}, class InterfaceRequiringGObjectInterface extends GObject.Interface {
    optionalG() {
        return `InterfaceRequiringGObjectInterface.optionalG()\n${
            AGObjectInterface.optionalG(this)}`;
    }
});

const GObjectImplementingGObjectInterface = GObject.registerClass({
    Implements: [AGObjectInterface],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
        'class-prop': GObject.ParamSpec.string('class-prop', 'Class property',
            'A property that is not on the interface',
            GObject.ParamFlags.READABLE, 'meh'),
    },
    Signals: {
        'class-signal': {},
    },
}, class GObjectImplementingGObjectInterface extends GObject.Object {
    get interface_prop() {
        return 'foobar';
    }

    get class_prop() {
        return 'meh';
    }

    requiredG() {}

    optionalG() {
        return AGObjectInterface.optionalG(this);
    }
});

const MinimalImplementationOfAGObjectInterface = GObject.registerClass({
    Implements: [AGObjectInterface],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
    },
}, class MinimalImplementationOfAGObjectInterface extends GObject.Object {
    requiredG() {}
});

const ImplementationOfTwoInterfaces = GObject.registerClass({
    Implements: [AGObjectInterface, InterfaceRequiringGObjectInterface],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
    },
}, class ImplementationOfTwoInterfaces extends GObject.Object {
    requiredG() {}

    optionalG() {
        return InterfaceRequiringGObjectInterface.optionalG(this);
    }
});

const ImplementationOfIntrospectedInterface = GObject.registerClass({
    Implements: [Gio.Action],
    Properties: {
        'enabled': GObject.ParamSpec.override('enabled', Gio.Action),
        'name': GObject.ParamSpec.override('name', Gio.Action),
        'state': GObject.ParamSpec.override('state', Gio.Action),
        'state-type': GObject.ParamSpec.override('state-type', Gio.Action),
        'parameter-type': GObject.ParamSpec.override('parameter-type',
            Gio.Action),
    },
}, class ImplementationOfIntrospectedInterface extends GObject.Object {
    get name() {
        return 'inaction';
    }
});

describe('GObject interface', function () {
    it('cannot be instantiated', function () {
        expect(() => new AGObjectInterface()).toThrow();
    });

    it('has a name', function () {
        expect(AGObjectInterface.name).toEqual('AGObjectInterface');
    });

    it('reports its type name', function () {
        expect(AGObjectInterface.$gtype.name).toEqual('ArbitraryGTypeName');
    });

    it('can be implemented by a GObject class', function () {
        let obj;
        expect(() => {
            obj = new GObjectImplementingGObjectInterface();
        }).not.toThrow();
        expect(obj instanceof AGObjectInterface).toBeTruthy();
    });

    it('is implemented by a GObject class with the correct class object', function () {
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.constructor).toBe(GObjectImplementingGObjectInterface);
        expect(obj.constructor.name)
            .toEqual('GObjectImplementingGObjectInterface');
    });

    it('can have its required function implemented', function () {
        expect(() => {
            let obj = new GObjectImplementingGObjectInterface();
            obj.requiredG();
        }).not.toThrow();
    });

    it('must have its required function implemented', function () {
        const BadObject = GObject.registerClass({
            Implements: [AGObjectInterface],
            Properties: {
                'interface-prop': GObject.ParamSpec.override('interface-prop',
                    AGObjectInterface),
            },
        }, class BadObject extends GObject.Object {});
        expect(() => new BadObject().requiredG())
            .toThrowError(GObject.NotImplementedError);
    });

    it("doesn't have to have its optional function implemented", function () {
        let obj;
        expect(() => {
            obj = new MinimalImplementationOfAGObjectInterface();
        }).not.toThrow();
        expect(obj instanceof AGObjectInterface).toBeTruthy();
    });

    it('can have its optional function deferred to by the implementation', function () {
        let obj = new MinimalImplementationOfAGObjectInterface();
        expect(obj.optionalG()).toEqual('AGObjectInterface.optionalG()');
    });

    it('can have its function chained up to', function () {
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.optionalG()).toEqual('AGObjectInterface.optionalG()');
    });

    it('can require another interface', function () {
        let obj;
        expect(() => {
            obj = new ImplementationOfTwoInterfaces();
        }).not.toThrow();
        expect(obj instanceof AGObjectInterface).toBeTruthy();
        expect(obj instanceof InterfaceRequiringGObjectInterface).toBeTruthy();
    });

    it('can chain up to another interface', function () {
        let obj = new ImplementationOfTwoInterfaces();
        expect(obj.optionalG())
            .toEqual('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()');
    });

    it("defers to the last interface's optional function", function () {
        const MinimalImplementationOfTwoInterfaces = GObject.registerClass({
            Implements: [AGObjectInterface, InterfaceRequiringGObjectInterface],
            Properties: {
                'interface-prop': GObject.ParamSpec.override('interface-prop',
                    AGObjectInterface),
            },
        }, class MinimalImplementationOfTwoInterfaces extends GObject.Object {
            requiredG() {}
        });
        let obj = new MinimalImplementationOfTwoInterfaces();
        expect(obj.optionalG())
            .toEqual('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()');
    });

    it('must be implemented by a class that implements all required interfaces', function () {
        expect(() => GObject.registerClass({
            Implements: [InterfaceRequiringGObjectInterface],
        }, class BadObject {
            required() {}
        })).toThrow();
    });

    it('must be implemented by a class that implements required interfaces in correct order', function () {
        expect(() => GObject.registerClass({
            Implements: [InterfaceRequiringGObjectInterface, AGObjectInterface],
        }, class BadObject {
            required() {}
        })).toThrow();
    });

    it('can require an interface from C', function () {
        const InitableInterface = GObject.registerClass({
            Requires: [GObject.Object, Gio.Initable],
        }, class InitableInterface extends GObject.Interface {});
        expect(() => GObject.registerClass({
            Implements: [InitableInterface],
        }, class BadObject {})).toThrow();
    });

    it('can connect class signals on the implementing class', function (done) {
        function quitLoop() {
            expect(classSignalSpy).toHaveBeenCalled();
            done();
        }
        let obj = new GObjectImplementingGObjectInterface();
        let classSignalSpy = jasmine.createSpy('classSignalSpy')
            .and.callFake(quitLoop);
        obj.connect('class-signal', classSignalSpy);
        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            obj.emit('class-signal');
            return GLib.SOURCE_REMOVE;
        });
    });

    it('can connect interface signals on the implementing class', function (done) {
        function quitLoop() {
            expect(interfaceSignalSpy).toHaveBeenCalled();
            done();
        }
        let obj = new GObjectImplementingGObjectInterface();
        let interfaceSignalSpy = jasmine.createSpy('interfaceSignalSpy')
            .and.callFake(quitLoop);
        obj.connect('interface-signal', interfaceSignalSpy);
        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            obj.emit('interface-signal');
            return GLib.SOURCE_REMOVE;
        });
    });

    it('can define properties on the implementing class', function () {
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.interface_prop).toEqual('foobar');
        expect(obj.class_prop).toEqual('meh');
    });

    it('must have its properties overridden', function () {
        // Failing to override an interface property doesn't raise an error but
        // instead logs a critical warning.
        GLib.test_expect_message('GLib-GObject', GLib.LogLevelFlags.LEVEL_CRITICAL,
            "Object class * doesn't implement property 'interface-prop' from " +
            "interface 'ArbitraryGTypeName'");
        GObject.registerClass({
            Implements: [AGObjectInterface],
        }, class MyNaughtyObject extends GObject.Object {
            requiredG() {}
        });
        // g_test_assert_expected_messages() is a macro, not introspectable
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectInterface.js',
            253, 'testGObjectMustOverrideInterfaceProperties');
    });

    it('can have introspected properties overriden', function () {
        let obj = new ImplementationOfIntrospectedInterface();
        expect(obj.name).toEqual('inaction');
    });

    it('can be implemented by a class as well as its parent class', function () {
        const SubObject = GObject.registerClass(
            class SubObject extends GObjectImplementingGObjectInterface {});
        let obj = new SubObject();
        expect(obj instanceof AGObjectInterface).toBeTruthy();
        expect(obj.interface_prop).toEqual('foobar');  // override not needed
    });

    it('can be reimplemented by a subclass of a class that already implements it', function () {
        const SubImplementer = GObject.registerClass({
            Implements: [AGObjectInterface],
        }, class SubImplementer extends GObjectImplementingGObjectInterface {});
        let obj = new SubImplementer();
        expect(obj instanceof AGObjectInterface).toBeTruthy();
        expect(obj.interface_prop).toEqual('foobar');  // override not needed
    });

    it('has a toString() defintion', function () {
        expect(new GObjectImplementingGObjectInterface().toString()).toMatch(
            /\[object instance wrapper GType:Gjs_GObjectImplementingGObjectInterface jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });

    it('has instance definition', function () {
        const obj = new GObjectImplementingGObjectInterface();
        const obj2 = new ImplementationOfTwoInterfaces();
        const file = Gio.File.new_for_path('/');
        expect(obj).toBeInstanceOf(AGObjectInterface);
        expect(obj).not.toBeInstanceOf(InterfaceRequiringGObjectInterface);
        expect(obj2).toBeInstanceOf(AGObjectInterface);
        expect(obj2).toBeInstanceOf(InterfaceRequiringGObjectInterface);
        expect(new GObject.Object()).not.toBeInstanceOf(AGObjectInterface);
        expect(file).toBeInstanceOf(Gio.File);
        expect(file).toBeInstanceOf(GObject.Object);
    });

    it('has instance definition for non-object type', function () {
        expect(null).not.toBeInstanceOf(AGObjectInterface);
        expect(true).not.toBeInstanceOf(AGObjectInterface);
        expect(undefined).not.toBeInstanceOf(AGObjectInterface);
        expect(123456).not.toBeInstanceOf(AGObjectInterface);
        expect(54321n).not.toBeInstanceOf(AGObjectInterface);
        expect('no way!').not.toBeInstanceOf(AGObjectInterface);
        expect(new Date()).not.toBeInstanceOf(AGObjectInterface);
    });

    it('has instance definition for non-object type for native interface', function () {
        expect(null).not.toBeInstanceOf(Gio.File);
        expect(true).not.toBeInstanceOf(Gio.File);
        expect(undefined).not.toBeInstanceOf(Gio.File);
        expect(12345).not.toBeInstanceOf(Gio.File);
        expect(54321n).not.toBeInstanceOf(Gio.File);
        expect('no way!').not.toBeInstanceOf(Gio.File);
        expect(new Date()).not.toBeInstanceOf(Gio.File);
    });

    describe('prototype', function () {
        let file, originalDup;

        beforeAll(function () {
            file = Gio.File.new_for_path('/');
            originalDup = Gio.File.prototype.dup;
        });

        it('toString is enumerable and defined', function () {
            expect(Object.getOwnPropertyNames(Gio.File.prototype)).toContain('toString');
            expect(Gio.File.prototype.toString).toBeDefined();
        });

        it('method properties are enumerated', function () {
            const expectedMethods = [
                'copy_attributes',
                'copy_async',
                'create_async',
                'create_readwrite_async',
                'delete_async',
                'enumerate_children',
            ];

            const methods = Object.getOwnPropertyNames(Gio.File.prototype);
            expect(methods).toEqual(jasmine.arrayContaining(expectedMethods));
        });

        it('method properties are defined', function () {
            const methods = Object.getOwnPropertyNames(Gio.File.prototype);

            for (const method of methods) {
                expect(Gio.File.prototype[method]).toBeDefined();
                expect(Gio.File.prototype[method]).toBeInstanceOf(Function);
            }
        });

        it('overrides are inherited by implementing classes', function () {
            spyOn(Gio.File.prototype, 'dup');

            expect(file).toBeInstanceOf(Gio.File);
            expect(file).toBeInstanceOf(Gio._LocalFilePrototype.constructor);

            file.dup();
            expect(Gio.File.prototype.dup).toHaveBeenCalledOnceWith();

            Gio.File.prototype.dup = originalDup;
            expect(file.dup).toBe(originalDup);
        });

        it('overrides cannot be changed by instances of child classes', function () {
            spyOn(Gio.File.prototype, 'dup');

            expect(file).toBeInstanceOf(Gio.File);
            expect(file).toBeInstanceOf(Gio._LocalFilePrototype.constructor);

            file.dup = 5;
            expect(Gio.File.prototype.dup).not.toBe(5);
            expect(Gio._LocalFilePrototype.dup).not.toBe(5);

            file.dup = originalDup;
            expect(file.dup).toBe(originalDup);
        });

        it('unknown properties are inherited by implementing classes', function () {
            Gio.File.prototype._originalDup = originalDup;
            expect(file._originalDup).toBe(originalDup);

            Gio.File.prototype._originalDup = 5;
            expect(file._originalDup).toBe(5);

            delete Gio.File.prototype._originalDup;
            expect(file._originalDup).not.toBeDefined();
        });

        it('original property can be shadowed by class prototype property', function () {
            spyOn(Gio._LocalFilePrototype, 'dup').and.returnValue(5);

            expect(file.dup()).toBe(5);
            expect(Gio._LocalFilePrototype.dup).toHaveBeenCalled();
        });

        it('overridden property can be shadowed by class prototype property', function () {
            spyOn(Gio._LocalFilePrototype, 'dup');
            spyOn(Gio.File.prototype, 'dup');

            file.dup();
            expect(Gio._LocalFilePrototype.dup).toHaveBeenCalled();
            expect(Gio.File.prototype.dup).not.toHaveBeenCalled();
        });

        it('shadowed property can be restored', function () {
            Gio._LocalFilePrototype.dup = 5;
            expect(file.dup).toBe(5);

            delete Gio._LocalFilePrototype.dup;
            expect(file.dup).toBeInstanceOf(Function);
        });
    });
});

describe('Specific class and interface checks', function () {
    it('Gio.AsyncInitable must implement vfunc_async_init', function () {
        expect(() => GObject.registerClass({
            Implements: [Gio.Initable, Gio.AsyncInitable],
        }, class BadAsyncInitable extends GObject.Object {
            vfunc_init() {}
        })).toThrow();
    });
});
