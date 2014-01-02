// tests for imports.lang module

const JSUnit = imports.jsUnit;
const ByteArray = imports.byteArray;
const Gio = imports.gi.Gio;

function testEmptyByteArray() {
    let a = new ByteArray.ByteArray();
    JSUnit.assertEquals("length is 0 for empty array", 0, a.length);
}

function testInitialSizeByteArray() {
    let a = new ByteArray.ByteArray(10);
    JSUnit.assertEquals("length is 10 for initially-sized-10 array", 10, a.length);

    let i;

    for (i = 0; i < a.length; ++i) {
        JSUnit.assertEquals("new array initialized to zeroes", 0, a[i]);
    }

    JSUnit.assertEquals("array had proper number of elements post-construct (counting for)",
                 10, i);
}

function testAssignment() {
    let a = new ByteArray.ByteArray(256);
    JSUnit.assertEquals("length is 256 for initially-sized-256 array", 256, a.length);

    let i;
    let count;

    count = 0;
    for (i = 0; i < a.length; ++i) {
        JSUnit.assertEquals("new array initialized to zeroes", 0, a[i]);
        a[i] = 255 - i;
        count += 1;
    }

    JSUnit.assertEquals("set proper number of values", 256, count);

    count = 0;
    for (i = 0; i < a.length; ++i) {
        JSUnit.assertEquals("assignment set expected value", 255 - i, a[i]);
        count += 1;
    }

    JSUnit.assertEquals("checked proper number of values", 256, count);
}

function testAssignmentPastEnd() {
    let a = new ByteArray.ByteArray();
    JSUnit.assertEquals("length is 0 for empty array", 0, a.length);

    a[2] = 5;
    JSUnit.assertEquals("implicitly made length 3", 3, a.length);
    JSUnit.assertEquals("implicitly-created zero byte", 0, a[0]);
    JSUnit.assertEquals("implicitly-created zero byte", 0, a[1]);
    JSUnit.assertEquals("stored 5 in autocreated position", 5, a[2]);
}

function testAssignmentToLength() {
    let a = new ByteArray.ByteArray(20);
    JSUnit.assertEquals("length is 20 for new array", 20, a.length);

    a.length = 5;

    JSUnit.assertEquals("length is 5 after setting it to 5", 5, a.length);
}

function testNonIntegerAssignment() {
    let a = new ByteArray.ByteArray();

    a[0] = 5;
    JSUnit.assertEquals("assigning 5 gives a byte 5", 5, a[0]);

    a[0] = null;
    JSUnit.assertEquals("assigning null gives a zero byte", 0, a[0]);

    a[0] = 5;
    JSUnit.assertEquals("assigning 5 gives a byte 5", 5, a[0]);

    a[0] = undefined;
    JSUnit.assertEquals("assigning undefined gives a zero byte", 0, a[0]);

    a[0] = 3.14;
    JSUnit.assertEquals("assigning a double rounds off", 3, a[0]);
}

function testFromString() {
    let a = ByteArray.fromString('abcd');
    JSUnit.assertEquals("from string 'abcd' gives length 4", 4, a.length);
    JSUnit.assertEquals("'a' results in 97", 97, a[0]);
    JSUnit.assertEquals("'b' results in 98", 98, a[1]);
    JSUnit.assertEquals("'c' results in 99", 99, a[2]);
    JSUnit.assertEquals("'d' results in 100", 100, a[3]);
}

function testFromArray() {
    let a = ByteArray.fromArray([ 1, 2, 3, 4 ]);
    JSUnit.assertEquals("from array [1,2,3,4] gives length 4", 4, a.length);
    JSUnit.assertEquals("a[0] == 1", 1, a[0]);
    JSUnit.assertEquals("a[1] == 2", 2, a[1]);
    JSUnit.assertEquals("a[2] == 3", 3, a[2]);
    JSUnit.assertEquals("a[3] == 4", 4, a[3]);
}

function testToString() {
    let a = new ByteArray.ByteArray();
    a[0] = 97;
    a[1] = 98;
    a[2] = 99;
    a[3] = 100;

    let s = a.toString();
    JSUnit.assertEquals("toString() on 4 ascii bytes gives length 4", 4, s.length);
    JSUnit.assertEquals("toString() gives 'abcd'", "abcd", s);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

