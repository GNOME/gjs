// application/javascript;version=1.8
var Mainloop = imports.mainloop;

function testBasicMainloop() {
    log('running mainloop test');
    Mainloop.idle_add(function() { Mainloop.quit('testMainloop'); });
    Mainloop.run('testMainloop');
    log('mainloop test done');
}

/* A dangling mainloop idle should get removed and not leaked */
function testDanglingIdle() {
    Mainloop.idle_add(function() { return true; });
}

gjstestRun();
