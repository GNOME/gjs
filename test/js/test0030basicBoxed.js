var Regress = imports.gi.Regress;

function testBasicBoxed() {
    var a = new Regress.TestSimpleBoxedA();
    a.some_int = 42;
}

gjstestRun();
