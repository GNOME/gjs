// -*- mode: js; indent-tabs-mode: nil -*-

const Lang = imports.lang;

const AnInterface = new Lang.Interface({
    Name: 'AnInterface',

    required: Lang.Interface.UNIMPLEMENTED,

    optional: function () {
        return 'AnInterface.optional()';
    },

    optionalGeneric: function () {
        return 'AnInterface.optionalGeneric()';
    },

    argumentGeneric: function (arg) {
        return 'AnInterface.argumentGeneric(' + arg + ')';
    },

    usesThis: function () {
        return this._interfacePrivateMethod();
    },

    _interfacePrivateMethod: function () {
        return 'interface private method';
    },

    get some_prop() {
        return 'AnInterface.some_prop getter';
    },

    set some_prop(value) {
        this.some_prop_setter_called = true;
    }
});

const InterfaceRequiringOtherInterface = new Lang.Interface({
    Name: 'InterfaceRequiringOtherInterface',
    Requires: [ AnInterface ],

    optional: function () {
        return 'InterfaceRequiringOtherInterface.optional()\n' +
            AnInterface.prototype.optional.apply(this, arguments);
    },

    optionalGeneric: function () {
        return 'InterfaceRequiringOtherInterface.optionalGeneric()\n' +
            AnInterface.optionalGeneric(this);
    }
});

const ObjectImplementingAnInterface = new Lang.Class({
    Name: 'ObjectImplementingAnInterface',
    Implements: [ AnInterface ],

    _init: function () {
        this.parent();
    },

    required: function () {},

    optional: function () {
        return AnInterface.prototype.optional.apply(this, arguments);
    },

    optionalGeneric: function () {
        return AnInterface.optionalGeneric(this);
    },

    argumentGeneric: function (arg) {
        return AnInterface.argumentGeneric(this, arg + ' (hello from class)');
    }
});

const InterfaceRequiringClassAndInterface = new Lang.Interface({
    Name: 'InterfaceRequiringClassAndInterface',
    Requires: [ ObjectImplementingAnInterface, InterfaceRequiringOtherInterface ],
});

const MinimalImplementationOfAnInterface = new Lang.Class({
    Name: 'MinimalImplementationOfAnInterface',
    Implements: [ AnInterface ],

    required: function () {}
});

const ImplementationOfTwoInterfaces = new Lang.Class({
    Name: 'ImplementationOfTwoInterfaces',
    Implements: [ AnInterface, InterfaceRequiringOtherInterface ],

    required: function () {},

    optional: function () {
        return InterfaceRequiringOtherInterface.prototype.optional.apply(this, arguments);
    },

    optionalGeneric: function () {
        return InterfaceRequiringOtherInterface.optionalGeneric(this);
    }
});

describe('An interface', function () {
    it('is an instance of Lang.Interface', function () {
        expect(AnInterface instanceof Lang.Interface).toBeTruthy();
        expect(InterfaceRequiringOtherInterface instanceof Lang.Interface).toBeTruthy();
    });

    it('cannot be instantiated', function () {
        expect(() => new AnInterface()).toThrow();
    });

    it('can be implemented by a class', function () {
        let obj;
        expect(() => { obj = new ObjectImplementingAnInterface(); }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it("can be implemented by a class's superclass", function () {
        const ChildWhoseParentImplementsAnInterface = new Lang.Class({
            Name: "ChildWhoseParentImplementsAnInterface",
            Extends: ObjectImplementingAnInterface
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
        expect(() => obj = new MinimalImplementationOfAnInterface()).not.toThrow();
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
            Implements: [ AnInterface ],
            required: function () {},
            get some_prop() {
                return 'ObjectWithGetter.some_prop getter';
            }
        });
        let obj = new ObjectWithGetter();
        expect(obj.some_prop).toEqual('ObjectWithGetter.some_prop getter');
    });

    it('can have its property setter overridden', function () {
        const ObjectWithSetter = new Lang.Class({
            Name: 'ObjectWithSetter',
            Implements: [ AnInterface ],
            required: function () {},
            set some_prop(value) {  /* setter without getter */// jshint ignore:line
                this.overridden_some_prop_setter_called = true;
            }
        });
        let obj = new ObjectWithSetter();
        obj.some_prop = 'foobar';
        expect(obj.overridden_some_prop_setter_called).toBeTruthy();
        expect(obj.some_prop_setter_called).not.toBeDefined();
    });

    it('can require another interface', function () {
        let obj;
        expect(() => { obj = new ImplementationOfTwoInterfaces(); }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(obj.constructor.implements(InterfaceRequiringOtherInterface)).toBeTruthy();
    });

    it('can have empty requires', function () {
        expect(() => new Lang.Interface({
            Name: 'InterfaceWithEmptyRequires',
            Requires: []
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
            Implements: [ AnInterface, InterfaceRequiringOtherInterface ],

            required: function () {}
        });
        let obj = new MinimalImplementationOfTwoInterfaces();
        expect(obj.optionalGeneric())
            .toEqual('InterfaceRequiringOtherInterface.optionalGeneric()\nAnInterface.optionalGeneric()');
    });

    it('must have all its required interfaces implemented', function () {
        expect(() => new Lang.Class({
            Name: 'ObjectWithNotEnoughInterfaces',
            Implements: [ InterfaceRequiringOtherInterface ],
            required: function () {}
        })).toThrow();
    });

    it('must have all its required interfaces implemented in the correct order', function () {
        expect(() => new Lang.Class({
            Name: 'ObjectWithInterfacesInWrongOrder',
            Implements: [ InterfaceRequiringOtherInterface, AnInterface ],
            required: function () {}
        })).toThrow();
    });

    it('can have its implementation on a parent class', function () {
        let obj;
        expect(() => {
            const ObjectInheritingFromInterfaceImplementation = new Lang.Class({
                Name: 'ObjectInheritingFromInterfaceImplementation',
                Extends: ObjectImplementingAnInterface,
                Implements: [ InterfaceRequiringOtherInterface ],
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
                Implements: [ InterfaceRequiringOtherInterface, InterfaceRequiringClassAndInterface ]
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
            Implements: [ AnInterface, InterfaceRequiringOtherInterface, InterfaceRequiringClassAndInterface ],
            required: function () {},
        })).toThrow();
    });

    it('can have methods that call others of its methods', function () {
        let obj = new ObjectImplementingAnInterface();
        expect(obj.usesThis()).toEqual('interface private method');
    });

    it('is implemented by a subclass of a class that implements it', function () {
        const SubObject = new Lang.Class({
            Name: 'SubObject',
            Extends: ObjectImplementingAnInterface
        });
        let obj = new SubObject();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it('can be reimplemented by a subclass of a class that implements it', function () {
        const SubImplementer = new Lang.Class({
            Name: 'SubImplementer',
            Extends: ObjectImplementingAnInterface,
            Implements: [ AnInterface ]
        });
        let obj = new SubImplementer();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(() => obj.required()).not.toThrow();
    });

    it('tells what it is with toString()', function () {
        expect(AnInterface.toString()).toEqual('[interface Interface for AnInterface]');
    });
});
