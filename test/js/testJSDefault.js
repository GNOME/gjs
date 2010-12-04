// Test that we *don't* get SpiderMonkey extensions; see
// testJS1_8.js.

var GLib = imports.gi.GLib;

function testLet() {
    assertRaises('missing ; before statement', function () { eval("let result = 1+1; result;") });
}

function testYield() {
    assertRaises('missing ; before statement', function() { eval("function foo () { yield 42; }; foo();"); });
}

gjstestRun();
