// -*- mode: js; indent-tabs-mode: nil -*-

const JSUnit = imports.jsUnit;
const Lang = imports.lang;

function assertArrayEquals(expected, got) {
    JSUnit.assertEquals(expected.length, got.length);
    for (let i = 0; i < expected.length; i ++) {
        JSUnit.assertEquals(expected[i], got[i]);
    }
}

const MagicBase = new Lang.Class({
    Name: 'MagicBase',

    _init: function(a, buffer) {
        if (buffer) buffer.push(a);
        this.a = a;
    },

    foo: function(a, buffer) {
        buffer.push(a);
        return a * 3;
    },

    bar: function(a) {
        return a * 5;
    }
});

const Magic = new Lang.Class({
    Name: 'Magic',

    Extends: MagicBase,

    _init: function(a, b, buffer) {
        this.parent(a, buffer);
        if (buffer) buffer.push(b);
        this.b = b;
    },

    foo: function(a, b, buffer) {
        let val = this.parent(a, buffer);
        buffer.push(b);
        return val * 2;
    },

    bar: function(a, buffer) {
        this.foo(a, 2*a, buffer);
        return this.parent(a);
    }
});

const ToStringOverride = new Lang.Class({
    Name: 'ToStringOverride',

    toString: function() {
        let oldToString = this.parent();
        return oldToString + '; hello';
    }
});

const Accessor = new Lang.Class({
    Name: 'AccessorMagic',

    _init: function(val) {
        this._val = val;
    },

    get value() {
        return this._val;
    },

    set value(val) {
        if (val != 42)
            throw TypeError('Value is not a magic number');
        this._val = val;
    }
});

const AbstractBase = new Lang.Class({
    Name: 'AbstractBase',
    Abstract: true,

    _init: function() {
        this.foo = 42;
    }
});

const AbstractImpl = new Lang.Class({
    Name: 'AbstractImpl',
    Extends: AbstractBase,

    _init: function() {
        this.parent();
        this.bar = 42;
    }
});

const AbstractImpl2 = new Lang.Class({
    Name: 'AbstractImpl2',
    Extends: AbstractBase,

    // no _init here, we inherit the parent one
});

const CustomConstruct = new Lang.Class({
    Name: 'CustomConstruct',

    _construct: function(one, two) {
        return [one, two];
    }
});

function testClassFramework() {
    let newMagic = new MagicBase('A');
    JSUnit.assertEquals('A',  newMagic.a);
}

function testInheritance() {
    let buffer = [];

    let newMagic = new Magic('a', 'b', buffer);
    assertArrayEquals(['a', 'b'], buffer);

    buffer = [];
    let val = newMagic.foo(10, 20, buffer);
    assertArrayEquals([10, 20], buffer);
    JSUnit.assertEquals(10*6, val);
}

function testConstructor() {
    JSUnit.assertEquals(Magic, Magic.prototype.constructor);

    let newMagic = new Magic();
    JSUnit.assertEquals(Magic, newMagic.constructor);
}

function testInstanceOf() {
    let newMagic = new Magic();

    JSUnit.assertTrue(newMagic instanceof Magic);
    JSUnit.assertTrue(newMagic instanceof MagicBase);
}

function testToString() {
    let newMagic = new MagicBase();
    JSUnit.assertEquals('[object MagicBase]', newMagic.toString());

    let override = new ToStringOverride();
    JSUnit.assertEquals('[object ToStringOverride]; hello', override.toString());
}

function testConfigurable() {
    let newMagic = new MagicBase();

    delete newMagic.foo;
    JSUnit.assertNotUndefined(newMagic.foo);
}

function testAccessor() {
    let newAccessor = new Accessor(11);

    JSUnit.assertEquals(11, newAccessor.value);
    JSUnit.assertRaises(function() {
        newAccessor.value = 12;
    });

    newAccessor.value = 42;
    JSUnit.assertEquals(42, newAccessor.value);
}

function testAbstract() {
    JSUnit.assertRaises(function() {
        let newAbstract = new AbstractBase();
    });

    let newAbstract = new AbstractImpl();
    JSUnit.assertEquals(42, newAbstract.foo);
    JSUnit.assertEquals(42, newAbstract.bar);

    newAbstract = new AbstractImpl2();
    JSUnit.assertEquals(42, newAbstract.foo);
}

function testCrossCall() {
    // test that a method can call another without clobbering
    // __caller__
    let newMagic = new Magic();
    let buffer = [];

    let res = newMagic.bar(10, buffer);
    assertArrayEquals([10, 20], buffer);
    JSUnit.assertEquals(50, res);
}

function testConstruct() {
    let instance = new CustomConstruct(1, 2);

    JSUnit.assertTrue(instance instanceof Array);
    JSUnit.assertTrue(!(instance instanceof CustomConstruct));

    assertArrayEquals([1, 2], instance);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
