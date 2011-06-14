// application/javascript;version=1.8

// Test SpiderMonkey JS extensions; see
// https://developer.mozilla.org/en/JavaScript/New_in_JavaScript/1.8

// "const"
const Everything = imports.gi.Regress;

function testLet() {
    // "let"
    let foo = "bar";
    let cow = "moo";

    assertEquals(foo, "bar");
}

function testMultiReturn() {
    // "destructuring bind"
    let [y, z, q] = Everything.test_torture_signature_0(42, 'foo', 7);
    assertEquals(z, 84);
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

    assertEquals(v[0], 0);
    assertEquals(v[1], 1);
    assertEquals(v[2], 1);
    assertEquals(v[3], 2);
    assertEquals(v[4], 3);
    assertEquals(v[5], 5);
    assertEquals(v[6], 8);
    assertEquals(v[7], 13);
    assertEquals(v[8], 21);
    assertEquals(v[9], 34);
}

gjstestRun();
