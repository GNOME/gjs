const JSUnit = imports.jsUnit;
const Regress = imports.gi.Regress;

function testBasicBoxed() {
    var a = new Regress.TestSimpleBoxedA();
    a.some_int = 42;
    JSUnit.assertEquals(a.some_int, 42);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
