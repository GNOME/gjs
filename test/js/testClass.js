// application/javascript;version=1.8

const Lang = imports.lang;

function assertArrayEquals(expected, got) {
    assertEquals(expected.length, got.length);
    for (let i = 0; i < expected.length; i ++) {
        assertEquals(expected[i], got[i]);
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
    }
});

const ToStringOverride = new Lang.Class({
    Name: 'ToStringOverride',

    toString: function() {
        let oldToString = this.parent();
        return oldToString + '; hello';
    }
});

function testClassFramework() {
    let newMagic = new MagicBase('A');
    assertEquals('A',  newMagic.a);
}

function testInheritance() {
    let buffer = [];

    let newMagic = new Magic('a', 'b', buffer);
    assertArrayEquals(['a', 'b'], buffer);

    buffer = [];
    let val = newMagic.foo(10, 20, buffer);
    assertArrayEquals([10, 20], buffer);
    assertEquals(10*6, val);
}

function testConstructor() {
    assertEquals(Magic, Magic.prototype.constructor);

    let newMagic = new Magic();
    assertEquals(Magic, newMagic.constructor);
}

function testInstanceOf() {
    let newMagic = new Magic();

    assertTrue(newMagic instanceof Magic);
    assertTrue(newMagic instanceof MagicBase);
}

function testToString() {
    let newMagic = new MagicBase();
    assertEquals('[object MagicBase]', newMagic.toString());

    let override = new ToStringOverride();
    assertEquals('[object ToStringOverride]; hello', override.toString());
}

gjstestRun();
