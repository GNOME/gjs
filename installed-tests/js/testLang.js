// tests for imports.lang module

const JSUnit = imports.jsUnit;
const Lang = imports.lang;

function testCountProperties() {
    var foo = { 'a' : 10, 'b' : 11 };
    JSUnit.assertEquals("number of props", 2, Lang.countProperties(foo));
}

function testCopyProperties() {
    var foo = { 'a' : 10, 'b' : 11 };
    var bar = {};

    Lang.copyProperties(foo, bar);

    JSUnit.assertTrue("a in bar", ('a' in bar));
    JSUnit.assertTrue("b in bar", ('b' in bar));
    JSUnit.assertEquals("a is 10", 10, bar.a);
    JSUnit.assertEquals("b is 11", 11, bar.b);
    JSUnit.assertEquals("2 items in bar", 2, Lang.countProperties(bar));
}

function testCopyPublicProperties() {
    var foo = { 'a' : 10, 'b' : 11, '_c' : 12 };
    var bar = {};

    Lang.copyPublicProperties(foo, bar);

    JSUnit.assertTrue("a in bar", ('a' in bar));
    JSUnit.assertTrue("b in bar", ('b' in bar));
    JSUnit.assertFalse("_c in bar", ('_c' in bar));
    JSUnit.assertEquals("a is 10", 10, bar.a);
    JSUnit.assertEquals("b is 11", 11, bar.b);
    JSUnit.assertEquals("2 items in bar", 2, Lang.countProperties(bar));
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

    JSUnit.assertTrue("bar has 'c' getter", (getterFunc != null));
    JSUnit.assertTrue("bar has 'c' setter", (setterFunc != null));
    JSUnit.assertTrue("bar 'c' value is 10", (c == 10));
    JSUnit.assertTrue("bar 'a' new value is 13", (bar.a == 13));
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
    JSUnit.assertEquals(callback(), true);
    JSUnit.assertNotEquals("o.obj in callback", undefined, o.obj);
    JSUnit.assertEquals("o.obj in callback", o, o.obj);
    JSUnit.assertEquals("o.args in callback", 0, o.args.length);
    JSUnit.assertRaises(function() { return Lang.bind(o, undefined); });
    JSUnit.assertRaises(function() { return Lang.bind(undefined, function() {}); });

    let o2 = new Obj();
    callback = Lang.bind(o2, o2.callback, 42, 1138);
    JSUnit.assertEquals(callback(), true);
    JSUnit.assertNotEquals("o2.args in callback", undefined, o2.args);
    JSUnit.assertEquals("o2.args.length in callback", 2, o2.args.length);
    JSUnit.assertEquals("o2.args[0] in callback", 42, o2.args[0]);
    JSUnit.assertEquals("o2.args[1] in callback", 1138, o2.args[1]);

    let o3 = new Obj();
    callback = Lang.bind(o3, o3.callback, 42, 1138);
    JSUnit.assertEquals(callback(1, 2, 3), true);
    JSUnit.assertNotEquals("o3.args in callback", undefined, o3.args);
    JSUnit.assertEquals("o3.args.length in callback", 5, o3.args.length);
    JSUnit.assertEquals("o3.args[0] in callback", 1, o3.args[0]);
    JSUnit.assertEquals("o3.args[1] in callback", 2, o3.args[1]);
    JSUnit.assertEquals("o3.args[2] in callback", 3, o3.args[2]);
    JSUnit.assertEquals("o3.args[3] in callback", 42, o3.args[3]);
    JSUnit.assertEquals("o3.args[4] in callback", 1138, o3.args[4]);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

