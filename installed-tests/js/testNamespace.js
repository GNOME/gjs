const JSUnit = imports.jsUnit;
const Everything = imports.gi.Regress;

function testName() {
    JSUnit.assertEquals(Everything.__name__, "Regress");
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
