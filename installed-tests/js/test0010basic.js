const JSUnit = imports.jsUnit;
function testBasic1() {
    var foo = 1+1;
    JSUnit.assertEquals(2, foo);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
