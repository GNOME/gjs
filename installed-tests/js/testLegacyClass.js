// -*- mode: js; indent-tabs-mode: nil -*-
/* eslint-disable no-restricted-properties */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Jasper St. Pierre <jstpierre@mecheye.net>
// SPDX-FileCopyrightText: 2011 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2015 Endless Mobile, Inc.

const Lang = imports.lang;

const NormalClass = new Lang.Class({
    Name: 'NormalClass',

    _init() {
        this.one = 1;
    },
});

let Subclassed = [];
const MetaClass = new Lang.Class({
    Name: 'MetaClass',
    Extends: Lang.Class,

    _init(params) {
        Subclassed.push(params.Name);
        this.parent(params);

        if (params.Extended) {
            this.prototype.dynamic_method = this.wrapFunction('dynamic_method', function () {
                return 73;
            });

            this.DYNAMIC_CONSTANT = 2;
        }
    },
});

const CustomMetaOne = new MetaClass({
    Name: 'CustomMetaOne',
    Extends: NormalClass,
    Extended: false,

    _init() {
        this.parent();

        this.two = 2;
    },
});

const CustomMetaTwo = new MetaClass({
    Name: 'CustomMetaTwo',
    Extends: NormalClass,
    Extended: true,

    _init() {
        this.parent();

        this.two = 2;
    },
});

// This should inherit CustomMeta, even though
// we use Lang.Class
const CustomMetaSubclass = new Lang.Class({
    Name: 'CustomMetaSubclass',
    Extends: CustomMetaOne,
    Extended: true,

    _init() {
        this.parent();

        this.three = 3;
    },
});

describe('A metaclass', function () {
    it('has its constructor called each time a class is created with it', function () {
        expect(Subclassed).toEqual(['CustomMetaOne', 'CustomMetaTwo',
            'CustomMetaSubclass']);
    });

    it('is an instance of Lang.Class', function () {
        expect(NormalClass instanceof Lang.Class).toBeTruthy();
        expect(MetaClass instanceof Lang.Class).toBeTruthy();
    });

    it('produces instances that are instances of itself and Lang.Class', function () {
        expect(CustomMetaOne instanceof Lang.Class).toBeTruthy();
        expect(CustomMetaOne instanceof MetaClass).toBeTruthy();
    });

    it('can dynamically define properties in its constructor', function () {
        expect(CustomMetaTwo.DYNAMIC_CONSTANT).toEqual(2);
        expect(CustomMetaOne.DYNAMIC_CONSTANT).not.toBeDefined();
    });

    describe('instance', function () {
        let instanceOne, instanceTwo;
        beforeEach(function () {
            instanceOne = new CustomMetaOne();
            instanceTwo = new CustomMetaTwo();
        });

        it('gets all the properties from its class and metaclass', function () {
            expect(instanceOne).toEqual(jasmine.objectContaining({one: 1, two: 2}));
            expect(instanceTwo).toEqual(jasmine.objectContaining({one: 1, two: 2}));
        });

        it('gets dynamically defined properties from metaclass', function () {
            expect(() => instanceOne.dynamic_method()).toThrow();
            expect(instanceTwo.dynamic_method()).toEqual(73);
        });
    });

    it('can be instantiated with Lang.Class but still get the appropriate metaclass', function () {
        expect(CustomMetaSubclass instanceof MetaClass).toBeTruthy();
        expect(CustomMetaSubclass.DYNAMIC_CONSTANT).toEqual(2);

        let instance = new CustomMetaSubclass();
        expect(instance).toEqual(jasmine.objectContaining({one: 1, two: 2, three: 3}));
        expect(instance.dynamic_method()).toEqual(73);
    });

    it('can be detected with Lang.getMetaClass', function () {
        expect(Lang.getMetaClass({
            Extends: CustomMetaOne,
        })).toBe(MetaClass);
    });
});

const MagicBase = new Lang.Class({
    Name: 'MagicBase',

    _init(a, buffer) {
        if (buffer)
            buffer.push(a);
        this.a = a;
    },

    foo(a, buffer) {
        buffer.push(a);
        return a * 3;
    },

    bar(a) {
        return a * 5;
    },
});

const Magic = new Lang.Class({
    Name: 'Magic',

    Extends: MagicBase,

    _init(a, b, buffer) {
        this.parent(a, buffer);
        if (buffer)
            buffer.push(b);
        this.b = b;
    },

    foo(a, b, buffer) {
        let val = this.parent(a, buffer);
        buffer.push(b);
        return val * 2;
    },

    bar(a, buffer) {
        this.foo(a, 2 * a, buffer);
        return this.parent(a);
    },
});

const Accessor = new Lang.Class({
    Name: 'AccessorMagic',

    _init(val) {
        this._val = val;
    },

    get value() {
        return this._val;
    },

    set value(val) {
        if (val !== 42)
            throw TypeError('Value is not a magic number');
        this._val = val;
    },
});

const AbstractBase = new Lang.Class({
    Name: 'AbstractBase',
    Abstract: true,

    _init() {
        this.foo = 42;
    },
});

describe('Class framework', function () {
    it('calls _init constructors', function () {
        let newMagic = new MagicBase('A');
        expect(newMagic.a).toEqual('A');
    });

    it('calls parent constructors', function () {
        let buffer = [];

        let newMagic = new Magic('a', 'b', buffer);
        expect(buffer).toEqual(['a', 'b']);

        buffer = [];
        let val = newMagic.foo(10, 20, buffer);
        expect(buffer).toEqual([10, 20]);
        expect(val).toEqual(10 * 6);
    });

    it('sets the right constructor properties', function () {
        expect(Magic.prototype.constructor).toBe(Magic);

        let newMagic = new Magic();
        expect(newMagic.constructor).toBe(Magic);
    });

    it('sets up instanceof correctly', function () {
        let newMagic = new Magic();

        expect(newMagic instanceof Magic).toBeTruthy();
        expect(newMagic instanceof MagicBase).toBeTruthy();
    });

    it('has a name', function () {
        expect(Magic.name).toEqual('Magic');
    });

    it('reports a sensible value for toString()', function () {
        let newMagic = new MagicBase();
        expect(newMagic.toString()).toEqual('[object MagicBase]');
    });

    it('allows overriding toString()', function () {
        const ToStringOverride = new Lang.Class({
            Name: 'ToStringOverride',

            toString() {
                let oldToString = this.parent();
                return `${oldToString}; hello`;
            },
        });

        let override = new ToStringOverride();
        expect(override.toString()).toEqual('[object ToStringOverride]; hello');
    });

    it('is not configurable', function () {
        let newMagic = new MagicBase();

        delete newMagic.foo;
        expect(newMagic.foo).toBeDefined();
    });

    it('allows accessors for properties', function () {
        let newAccessor = new Accessor(11);

        expect(newAccessor.value).toEqual(11);
        expect(() => (newAccessor.value = 12)).toThrow();

        newAccessor.value = 42;
        expect(newAccessor.value).toEqual(42);
    });

    it('raises an exception when creating an abstract class', function () {
        expect(() => new AbstractBase()).toThrow();
    });

    it('inherits properties from abstract base classes', function () {
        const AbstractImpl = new Lang.Class({
            Name: 'AbstractImpl',
            Extends: AbstractBase,

            _init() {
                this.parent();
                this.bar = 42;
            },
        });

        let newAbstract = new AbstractImpl();
        expect(newAbstract.foo).toEqual(42);
        expect(newAbstract.bar).toEqual(42);
    });

    it('inherits constructors from abstract base classes', function () {
        const AbstractImpl = new Lang.Class({
            Name: 'AbstractImpl',
            Extends: AbstractBase,
        });

        let newAbstract = new AbstractImpl();
        expect(newAbstract.foo).toEqual(42);
    });

    it('allows ES6 classes to inherit from abstract base classes', function () {
        class AbstractImpl extends AbstractBase {}

        let newAbstract = new AbstractImpl();
        expect(newAbstract.foo).toEqual(42);
    });

    it('lets methods call other methods without clobbering __caller__', function () {
        let newMagic = new Magic();
        let buffer = [];

        let res = newMagic.bar(10, buffer);
        expect(buffer).toEqual([10, 20]);
        expect(res).toEqual(50);
    });

    it('allows custom return values from constructors', function () {
        const CustomConstruct = new Lang.Class({
            Name: 'CustomConstruct',

            _construct(one, two) {
                return [one, two];
            },
        });

        let instance = new CustomConstruct(1, 2);

        expect(Array.isArray(instance)).toBeTruthy();
        expect(instance instanceof CustomConstruct).toBeFalsy();
        expect(instance).toEqual([1, 2]);
    });

    it('allows symbol-named methods', function () {
        const SymbolClass = new Lang.Class({
            Name: 'SymbolClass',
            *[Symbol.iterator]() {
                yield* [1, 2, 3];
            },
        });
        let instance = new SymbolClass();
        expect([...instance]).toEqual([1, 2, 3]);
    });
});

const AnInterface = new Lang.Interface({
    Name: 'AnInterface',

    required: Lang.Interface.UNIMPLEMENTED,

    optional() {
        return 'AnInterface.optional()';
    },

    optionalGeneric() {
        return 'AnInterface.optionalGeneric()';
    },

    argumentGeneric(arg) {
        return `AnInterface.argumentGeneric(${arg})`;
    },

    usesThis() {
        return this._interfacePrivateMethod();
    },

    _interfacePrivateMethod() {
        return 'interface private method';
    },

    get some_prop() {
        return 'AnInterface.some_prop getter';
    },

    set some_prop(value) {
        this.some_prop_setter_called = true;
    },
});

const InterfaceRequiringOtherInterface = new Lang.Interface({
    Name: 'InterfaceRequiringOtherInterface',
    Requires: [AnInterface],

    optional(...args) {
        return `InterfaceRequiringOtherInterface.optional()\n${
            AnInterface.prototype.optional.apply(this, args)}`;
    },

    optionalGeneric() {
        return `InterfaceRequiringOtherInterface.optionalGeneric()\n${
            AnInterface.optionalGeneric(this)}`;
    },
});

const ObjectImplementingAnInterface = new Lang.Class({
    Name: 'ObjectImplementingAnInterface',
    Implements: [AnInterface],

    _init() {
        this.parent();
    },

    required() {},

    optional(...args) {
        return AnInterface.prototype.optional.apply(this, args);
    },

    optionalGeneric() {
        return AnInterface.optionalGeneric(this);
    },

    argumentGeneric(arg) {
        return AnInterface.argumentGeneric(this, `${arg} (hello from class)`);
    },
});

const InterfaceRequiringClassAndInterface = new Lang.Interface({
    Name: 'InterfaceRequiringClassAndInterface',
    Requires: [ObjectImplementingAnInterface, InterfaceRequiringOtherInterface],
});

const MinimalImplementationOfAnInterface = new Lang.Class({
    Name: 'MinimalImplementationOfAnInterface',
    Implements: [AnInterface],

    required() {},
});

const ImplementationOfTwoInterfaces = new Lang.Class({
    Name: 'ImplementationOfTwoInterfaces',
    Implements: [AnInterface, InterfaceRequiringOtherInterface],

    required() {},

    optional(...args) {
        return InterfaceRequiringOtherInterface.prototype.optional.apply(this, args);
    },

    optionalGeneric() {
        return InterfaceRequiringOtherInterface.optionalGeneric(this);
    },
});

describe('An interface', function () {
    it('is an instance of Lang.Interface', function () {
        expect(AnInterface instanceof Lang.Interface).toBeTruthy();
        expect(InterfaceRequiringOtherInterface instanceof Lang.Interface).toBeTruthy();
    });

    it('has a name', function () {
        expect(AnInterface.name).toEqual('AnInterface');
    });

    it('cannot be instantiated', function () {
        expect(() => new AnInterface()).toThrow();
    });

    it('can be implemented by a class', function () {
        let obj;
        expect(() => {
            obj = new ObjectImplementingAnInterface();
        }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it("can be implemented by a class's superclass", function () {
        const ChildWhoseParentImplementsAnInterface = new Lang.Class({
            Name: 'ChildWhoseParentImplementsAnInterface',
            Extends: ObjectImplementingAnInterface,
        });
        let obj = new ChildWhoseParentImplementsAnInterface();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it("doesn't disturb a class's constructor", function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.constructor).toEqual(ObjectImplementingAnInterface);
    });

    it('can have its required method implemented', function () {
        let implementer = new ObjectImplementingAnInterface();
        expect(() => implementer.required()).not.toThrow();
    });

    it('must have a name', function () {
        expect(() => new Lang.Interface({
            required: Lang.Interface.UNIMPLEMENTED,
        })).toThrow();
    });

    it('must have its required methods implemented', function () {
        expect(() => new Lang.Class({
            Name: 'MyBadObject',
            Implements: [AnInterface],
        })).toThrow();
    });

    it('does not have to have its optional methods implemented', function () {
        let obj;
        expect(() => (obj = new MinimalImplementationOfAnInterface())).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it('can have its optional method deferred to by the implementation', function () {
        let obj = new MinimalImplementationOfAnInterface();
        expect(obj.optional()).toEqual('AnInterface.optional()');
    });

    it('can be chained up to by a class', function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.optional()).toEqual('AnInterface.optional()');
    });

    it('can include arguments when being chained up to by a class', function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.argumentGeneric('arg'))
            .toEqual('AnInterface.argumentGeneric(arg (hello from class))');
    });

    it('can have its property getter deferred to', function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.some_prop).toEqual('AnInterface.some_prop getter');
    });

    it('can have its property setter deferred to', function () {
        let obj = new ObjectImplementingAnInterface();
        obj.some_prop = 'foobar';
        expect(obj.some_prop_setter_called).toBeTruthy();
    });

    it('can have its property getter overridden', function () {
        const ObjectWithGetter = new Lang.Class({
            Name: 'ObjectWithGetter',
            Implements: [AnInterface],
            required() {},
            get some_prop() {
                return 'ObjectWithGetter.some_prop getter';
            },
        });
        let obj = new ObjectWithGetter();
        expect(obj.some_prop).toEqual('ObjectWithGetter.some_prop getter');
    });

    it('can have its property setter overridden', function () {
        const ObjectWithSetter = new Lang.Class({
            Name: 'ObjectWithSetter',
            Implements: [AnInterface],
            required() {},
            set some_prop(value) {  /* setter without getter */// jshint ignore:line
                this.overridden_some_prop_setter_called = true;
            },
        });
        let obj = new ObjectWithSetter();
        obj.some_prop = 'foobar';
        expect(obj.overridden_some_prop_setter_called).toBeTruthy();
        expect(obj.some_prop_setter_called).not.toBeDefined();
    });

    it('can require another interface', function () {
        let obj;
        expect(() => {
            obj = new ImplementationOfTwoInterfaces();
        }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(obj.constructor.implements(InterfaceRequiringOtherInterface)).toBeTruthy();
    });

    it('can have empty requires', function () {
        expect(() => new Lang.Interface({
            Name: 'InterfaceWithEmptyRequires',
            Requires: [],
        })).not.toThrow();
    });

    it('can chain up to another interface', function () {
        let obj = new ImplementationOfTwoInterfaces();
        expect(obj.optional())
            .toEqual('InterfaceRequiringOtherInterface.optional()\nAnInterface.optional()');
    });

    it('can be chained up to with a generic', function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.optionalGeneric()).toEqual('AnInterface.optionalGeneric()');
    });

    it('can chain up to another interface with a generic', function () {
        let obj = new ImplementationOfTwoInterfaces();
        expect(obj.optionalGeneric())
            .toEqual('InterfaceRequiringOtherInterface.optionalGeneric()\nAnInterface.optionalGeneric()');
    });

    it('has its optional function defer to that of the last interface', function () {
        const MinimalImplementationOfTwoInterfaces = new Lang.Class({
            Name: 'MinimalImplementationOfTwoInterfaces',
            Implements: [AnInterface, InterfaceRequiringOtherInterface],

            required() {},
        });
        let obj = new MinimalImplementationOfTwoInterfaces();
        expect(obj.optionalGeneric())
            .toEqual('InterfaceRequiringOtherInterface.optionalGeneric()\nAnInterface.optionalGeneric()');
    });

    it('must have all its required interfaces implemented', function () {
        expect(() => new Lang.Class({
            Name: 'ObjectWithNotEnoughInterfaces',
            Implements: [InterfaceRequiringOtherInterface],
            required() {},
        })).toThrow();
    });

    it('must have all its required interfaces implemented in the correct order', function () {
        expect(() => new Lang.Class({
            Name: 'ObjectWithInterfacesInWrongOrder',
            Implements: [InterfaceRequiringOtherInterface, AnInterface],
            required() {},
        })).toThrow();
    });

    it('can have its implementation on a parent class', function () {
        let obj;
        expect(() => {
            const ObjectInheritingFromInterfaceImplementation = new Lang.Class({
                Name: 'ObjectInheritingFromInterfaceImplementation',
                Extends: ObjectImplementingAnInterface,
                Implements: [InterfaceRequiringOtherInterface],
            });
            obj = new ObjectInheritingFromInterfaceImplementation();
        }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(obj.constructor.implements(InterfaceRequiringOtherInterface)).toBeTruthy();
    });

    it('can require its implementor to be a subclass of some class', function () {
        let obj;
        expect(() => {
            const ObjectImplementingInterfaceRequiringParentObject = new Lang.Class({
                Name: 'ObjectImplementingInterfaceRequiringParentObject',
                Extends: ObjectImplementingAnInterface,
                Implements: [InterfaceRequiringOtherInterface, InterfaceRequiringClassAndInterface],
            });
            obj = new ObjectImplementingInterfaceRequiringParentObject();
        }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(obj.constructor.implements(InterfaceRequiringOtherInterface)).toBeTruthy();
        expect(obj.constructor.implements(InterfaceRequiringClassAndInterface)).toBeTruthy();
    });

    it('must be implemented by an object which subclasses the required class', function () {
        expect(() => new Lang.Class({
            Name: 'ObjectWithoutRequiredParent',
            Implements: [AnInterface, InterfaceRequiringOtherInterface, InterfaceRequiringClassAndInterface],
            required() {},
        })).toThrow();
    });

    it('can have methods that call others of its methods', function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.usesThis()).toEqual('interface private method');
    });

    it('is implemented by a subclass of a class that implements it', function () {
        const SubObject = new Lang.Class({
            Name: 'SubObject',
            Extends: ObjectImplementingAnInterface,
        });
        let obj = new SubObject();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it('can be reimplemented by a subclass of a class that implements it', function () {
        const SubImplementer = new Lang.Class({
            Name: 'SubImplementer',
            Extends: ObjectImplementingAnInterface,
            Implements: [AnInterface],
        });
        let obj = new SubImplementer();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(() => obj.required()).not.toThrow();
    });

    it('tells what it is with toString()', function () {
        expect(AnInterface.toString()).toEqual('[interface Interface for AnInterface]');
    });
});

describe('ES6 class inheriting from Lang.Class', function () {
    let Shiny, Legacy;

    beforeEach(function () {
        Legacy = new Lang.Class({
            Name: 'Legacy',
            _init(someval) {
                this.constructorCalledWith = someval;
            },

            instanceMethod() {},
            chainUpToMe() {},
            overrideMe() {},

            get property() {
                return this._property + 1;
            },
            set property(value) {
                this._property = value - 2;
            },
        });
        Legacy.staticMethod = function () {};
        spyOn(Legacy, 'staticMethod');
        spyOn(Legacy.prototype, 'instanceMethod');
        spyOn(Legacy.prototype, 'chainUpToMe');
        spyOn(Legacy.prototype, 'overrideMe');

        Shiny = class extends Legacy {
            chainUpToMe() {
                super.chainUpToMe();
            }

            overrideMe() {}
        };
    });

    it('calls a static method on the parent class', function () {
        Shiny.staticMethod();
        expect(Legacy.staticMethod).toHaveBeenCalled();
    });

    it('calls a method on the parent class', function () {
        let instance = new Shiny();
        instance.instanceMethod();
        expect(Legacy.prototype.instanceMethod).toHaveBeenCalled();
    });

    it("passes arguments to the parent class's constructor", function () {
        let instance = new Shiny(42);
        expect(instance.constructorCalledWith).toEqual(42);
    });

    it('chains up to a method on the parent class', function () {
        let instance = new Shiny();
        instance.chainUpToMe();
        expect(Legacy.prototype.chainUpToMe).toHaveBeenCalled();
    });

    it('overrides a method on the parent class', function () {
        let instance = new Shiny();
        instance.overrideMe();
        expect(Legacy.prototype.overrideMe).not.toHaveBeenCalled();
    });

    it('sets and gets a property from the parent class', function () {
        let instance = new Shiny();
        instance.property = 42;
        expect(instance.property).toEqual(41);
    });
});
