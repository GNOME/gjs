
const JSUnit = imports.jsUnit;
const System = imports.system;

function testAddressOf() {
    let o1 = new Object();
    let o2 = new Object();

    JSUnit.assert(System.addressOf(o1) == System.addressOf(o1));
    JSUnit.assert(System.addressOf(o1) != System.addressOf(o2));
}

function testVersion() {
    JSUnit.assert(System.version >= 13600);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

