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

gjstestRun();
