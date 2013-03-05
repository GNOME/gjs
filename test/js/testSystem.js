// application/javascript;version=1.8

const System = imports.system;

function testAddressOf() {
    let o1 = new Object();
    let o2 = new Object();

    assert(System.addressOf(o1) == System.addressOf(o1));
    assert(System.addressOf(o1) != System.addressOf(o2));
}

gjstestRun();
