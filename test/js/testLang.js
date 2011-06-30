// application/javascript;version=1.8
// tests for imports.lang module

const Lang = imports.lang;

function testCountProperties() {
    var foo = { 'a' : 10, 'b' : 11 };
    assertEquals("number of props", 2, Lang.countProperties(foo));
}

function testCopyProperties() {
    var foo = { 'a' : 10, 'b' : 11 };
    var bar = {};

    Lang.copyProperties(foo, bar);

    assertTrue("a in bar", ('a' in bar));
    assertTrue("b in bar", ('b' in bar));
    assertEquals("a is 10", 10, bar.a);
    assertEquals("b is 11", 11, bar.b);
    assertEquals("2 items in bar", 2, Lang.countProperties(bar));
}

function testCopyPublicProperties() {
    var foo = { 'a' : 10, 'b' : 11, '_c' : 12 };
    var bar = {};

    Lang.copyPublicProperties(foo, bar);

    assertTrue("a in bar", ('a' in bar));
    assertTrue("b in bar", ('b' in bar));
    assertFalse("_c in bar", ('_c' in bar));
    assertEquals("a is 10", 10, bar.a);
    assertEquals("b is 11", 11, bar.b);
    assertEquals("2 items in bar", 2, Lang.countProperties(bar));
}

function testCopyGetterSetterProperties() {
    var foo = {
        'a' : 10,
        'b' : 11,
        get c() {
            return this.a;
        },
        set c(n) {
            this.a = n;
        }};
    var bar = {};

    Lang.copyProperties(foo, bar);

    let getterFunc = bar.__lookupGetter__("c");
    let setterFunc = bar.__lookupSetter__("c");

    // this should return the value of 'a'
    let c = bar.c;

    // this should set 'a' value
    bar.c = 13;

    assertTrue("bar has 'c' getter", (getterFunc != null));
    assertTrue("bar has 'c' setter", (setterFunc != null));
    assertTrue("bar 'c' value is 10", (c == 10));
    assertTrue("bar 'a' new value is 13", (bar.a == 13));
}

function testBind() {

    function Obj() {
    }

    Obj.prototype = {
        callback: function() {
            this.obj = this;
            this.args = arguments;
            return true;
        }
    };

    let callback;

    let o = new Obj();
    callback = Lang.bind(o, o.callback);
    assertEquals(callback(), true);
    assertNotEquals("o.obj in callback", undefined, o.obj);
    assertEquals("o.obj in callback", o, o.obj);
    assertEquals("o.args in callback", 0, o.args.length);
    assertRaises(function() { return Lang.bind(o, undefined); });
    assertRaises(function() { return Lang.bind(undefined, function() {}); });

    let o2 = new Obj();
    callback = Lang.bind(o2, o2.callback, 42, 1138);
    assertEquals(callback(), true);
    assertNotEquals("o2.args in callback", undefined, o2.args);
    assertEquals("o2.args.length in callback", 2, o2.args.length);
    assertEquals("o2.args[0] in callback", 42, o2.args[0]);
    assertEquals("o2.args[1] in callback", 1138, o2.args[1]);

    let o3 = new Obj();
    callback = Lang.bind(o3, o3.callback, 42, 1138);
    assertEquals(callback(1, 2, 3), true);
    assertNotEquals("o3.args in callback", undefined, o3.args);
    assertEquals("o3.args.length in callback", 5, o3.args.length);
    assertEquals("o3.args[0] in callback", 1, o3.args[0]);
    assertEquals("o3.args[1] in callback", 2, o3.args[1]);
    assertEquals("o3.args[2] in callback", 3, o3.args[2]);
    assertEquals("o3.args[3] in callback", 42, o3.args[3]);
    assertEquals("o3.args[4] in callback", 1138, o3.args[4]);
}

function testDefineAccessorProperty() {
    var obj = {};
    var storage = 42;

    assertEquals(obj.foo, undefined);

    Lang.defineAccessorProperty(obj, 'foo',
				function () { return storage; },
				function (val) { storage = val; });

    assertEquals(obj.foo, 42);
    obj.foo = 43;
    assertEquals(obj.foo, 43);
}

gjstestRun();
