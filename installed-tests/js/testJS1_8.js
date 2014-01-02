// Test SpiderMonkey JS extensions; see
// https://developer.mozilla.org/en/JavaScript/New_in_JavaScript/1.8

// "const"
const JSUnit = imports.jsUnit;
const Everything = imports.gi.Regress;

function testLet() {
    // "let"
    let foo = "bar";
    let cow = "moo";

    JSUnit.assertEquals(foo, "bar");
}

function testMultiReturn() {
    // "destructuring bind"
    let [y, z, q] = Everything.test_torture_signature_0(42, 'foo', 7);
    JSUnit.assertEquals(z, 84);
}

function testYield() {
    function fib() {
        var i = 0, j = 1;
        while (true) {
            yield i;
            var t = i;
            i = j;
            j += t;
        }
    }

    var v = [];
    var g = fib();
    for (var i = 0; i < 10; i++) {
        v.push(g.next());
    }

    JSUnit.assertEquals(v[0], 0);
    JSUnit.assertEquals(v[1], 1);
    JSUnit.assertEquals(v[2], 1);
    JSUnit.assertEquals(v[3], 2);
    JSUnit.assertEquals(v[4], 3);
    JSUnit.assertEquals(v[5], 5);
    JSUnit.assertEquals(v[6], 8);
    JSUnit.assertEquals(v[7], 13);
    JSUnit.assertEquals(v[8], 21);
    JSUnit.assertEquals(v[9], 34);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

