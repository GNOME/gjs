const JSUnit = imports.jsUnit;
// application/javascript;version=1.8
function testBasic1() {
    var foo = 1+1;
    JSUnit.assertEquals(2, foo);
}

function testFakeFailure() {
    var foo = 1+1;
    JSUnit.assertEquals(5, foo);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
