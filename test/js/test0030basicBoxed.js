// application/javascript;version=1.8
var Regress = imports.gi.Regress;

function testBasicBoxed() {
    var a = new Regress.TestSimpleBoxedA();
    a.some_int = 42;
}

gjstestRun();
