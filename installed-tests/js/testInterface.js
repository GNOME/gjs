// -*- mode: js; indent-tabs-mode: nil -*-

const JSUnit = imports.jsUnit;
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

function testInterfaceIsInstanceOfLangInterface() {
    JSUnit.assertTrue(AnInterface instanceof Lang.Interface);
    JSUnit.assertTrue(InterfaceRequiringOtherInterface instanceof Lang.Interface);
}

function testInterfaceCannotBeInstantiated() {
    JSUnit.assertRaises(() => new AnInterface());
}

function testObjectCanImplementInterface() {
    // Test will fail if the constructor throws an exception
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
}

function testSuperclassCanImplementInterface() {
    const ChildWhoseParentImplementsAnInterface = new Lang.Class({
        Name: "ChildWhoseParentImplementsAnInterface",
        Extends: ObjectImplementingAnInterface
    });
    let obj = new ChildWhoseParentImplementsAnInterface();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
}

function testObjectImplementingInterfaceHasCorrectConstructor() {
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertEquals(ObjectImplementingAnInterface, obj.constructor);
}

function testObjectCanImplementRequiredFunction() {
    // Test considered passing if no exception thrown
    let implementer = new ObjectImplementingAnInterface();
    implementer.required();
}

function testInterfaceMustHaveName() {
    JSUnit.assertRaises(function () {
        const AnInterfaceWithoutAName = new Lang.Interface({
            required: Lang.Interface.UNIMPLEMENTED
        });
    });
}

function testClassMustImplementRequiredFunction() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'MyBadObject',
        Implements: [ AnInterface ]
    }));
}

function testClassDoesntHaveToImplementOptionalFunction() {
    // Test will fail if the constructor throws an exception
    let obj = new MinimalImplementationOfAnInterface();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
}

function testObjectCanDeferToInterfaceOptionalFunction() {
    let obj = new MinimalImplementationOfAnInterface();
    JSUnit.assertEquals('AnInterface.optional()', obj.optional());
}

function testObjectCanChainUpToInterface() {
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertEquals('AnInterface.optional()', obj.optional());
}

function testObjectCanChainUpToInterfaceWithArguments() {
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertEquals('AnInterface.argumentGeneric(arg (hello from class))',
        obj.argumentGeneric('arg'));
}

function testObjectCanDeferToInterfaceGetter() {
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertEquals('AnInterface.some_prop getter', obj.some_prop);
}

function testObjectCanDeferToInterfaceSetter() {
    let obj = new ObjectImplementingAnInterface();
    obj.some_prop = 'foobar';
    JSUnit.assertTrue(obj.some_prop_setter_called);
}

function testObjectCanOverrideInterfaceGetter() {
    const ObjectWithGetter = new Lang.Class({
        Name: 'ObjectWithGetter',
        Implements: [ AnInterface ],
        required: function () {},
        get some_prop() {
            return 'ObjectWithGetter.some_prop getter';
        }
    });
    let obj = new ObjectWithGetter();
    JSUnit.assertEquals('ObjectWithGetter.some_prop getter', obj.some_prop);
}

function testObjectCanOverrideInterfaceSetter() {
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
    JSUnit.assertTrue(obj.overridden_some_prop_setter_called);
    JSUnit.assertUndefined(obj.some_prop_setter_called);
}

function testInterfaceCanRequireOtherInterface() {
    // Test will fail if the constructor throws an exception
    let obj = new ImplementationOfTwoInterfaces();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
    JSUnit.assertTrue(obj.constructor.implements(InterfaceRequiringOtherInterface));
}

function testRequiresCanBeEmpty() {
    // Test considered passing if no exception thrown
    const InterfaceWithEmptyRequires = new Lang.Interface({
        Name: 'InterfaceWithEmptyRequires',
        Requires: []
    });
}

function testInterfaceCanChainUpToOtherInterface() {
    let obj = new ImplementationOfTwoInterfaces();
    JSUnit.assertEquals('InterfaceRequiringOtherInterface.optional()\nAnInterface.optional()',
        obj.optional());
}

function testObjectCanChainUpToInterfaceWithGeneric() {
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertEquals('AnInterface.optionalGeneric()',
        obj.optionalGeneric());
}

function testInterfaceCanChainUpToOtherInterfaceWithGeneric() {
    let obj = new ImplementationOfTwoInterfaces();
    JSUnit.assertEquals('InterfaceRequiringOtherInterface.optionalGeneric()\nAnInterface.optionalGeneric()',
        obj.optionalGeneric());
}

function testObjectDefersToLastInterfaceOptionalFunction() {
    const MinimalImplementationOfTwoInterfaces = new Lang.Class({
        Name: 'MinimalImplementationOfTwoInterfaces',
        Implements: [ AnInterface, InterfaceRequiringOtherInterface ],

        required: function () {}
    });
    let obj = new MinimalImplementationOfTwoInterfaces();
    JSUnit.assertEquals('InterfaceRequiringOtherInterface.optionalGeneric()\nAnInterface.optionalGeneric()',
        obj.optionalGeneric());
}

function testClassMustImplementAllRequiredInterfaces() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'ObjectWithNotEnoughInterfaces',
        Implements: [ InterfaceRequiringOtherInterface ],
        required: function () {}
    }));
}

function testClassMustImplementRequiredInterfacesInCorrectOrder() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'ObjectWithInterfacesInWrongOrder',
        Implements: [ InterfaceRequiringOtherInterface, AnInterface ],
        required: function () {}
    }));
}

function testInterfacesCanBeImplementedOnAParentClass() {
    // Test will fail if the constructor throws an exception
    const ObjectInheritingFromInterfaceImplementation = new Lang.Class({
        Name: 'ObjectInheritingFromInterfaceImplementation',
        Extends: ObjectImplementingAnInterface,
        Implements: [ InterfaceRequiringOtherInterface ],
    });
    let obj = new ObjectInheritingFromInterfaceImplementation();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
    JSUnit.assertTrue(obj.constructor.implements(InterfaceRequiringOtherInterface));
}

function testInterfacesCanRequireBeingImplementedOnASubclass() {
    // Test will fail if the constructor throws an exception
    const ObjectImplementingInterfaceRequiringParentObject = new Lang.Class({
        Name: 'ObjectImplementingInterfaceRequiringParentObject',
        Extends: ObjectImplementingAnInterface,
        Implements: [ InterfaceRequiringOtherInterface, InterfaceRequiringClassAndInterface ]
    });
    let obj = new ObjectImplementingInterfaceRequiringParentObject();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
    JSUnit.assertTrue(obj.constructor.implements(InterfaceRequiringOtherInterface));
    JSUnit.assertTrue(obj.constructor.implements(InterfaceRequiringClassAndInterface));
}

function testObjectsMustSubclassIfRequired() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'ObjectWithoutRequiredParent',
        Implements: [ AnInterface, InterfaceRequiringOtherInterface, InterfaceRequiringClassAndInterface ],
        required: function () {},
    }));
}

function testInterfaceMethodsCanCallOtherInterfaceMethods() {
    let obj = new ObjectImplementingAnInterface();
    JSUnit.assertEquals('interface private method', obj.usesThis());
}

function testSubclassImplementsTheSameInterfaceAsItsParent() {
    const SubObject = new Lang.Class({
        Name: 'SubObject',
        Extends: ObjectImplementingAnInterface
    });
    let obj = new SubObject();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
}

function testSubclassCanReimplementTheSameInterfaceAsItsParent() {
    const SubImplementer = new Lang.Class({
        Name: 'SubImplementer',
        Extends: ObjectImplementingAnInterface,
        Implements: [ AnInterface ]
    });
    let obj = new SubImplementer();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
}

function testToString() {
    JSUnit.assertEquals('[interface Interface for AnInterface]',
                        AnInterface.toString());
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
