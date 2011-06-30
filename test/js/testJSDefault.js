// Test that we *don't* get SpiderMonkey extensions; see
// testJS1_8.js.
// application/javascript;version=ECMAv3

function testLet() {
    assertRaises('missing ; before statement', function () { eval("let result = 1+1; result;") });
}

function testYield() {
    assertRaises('missing ; before statement', function() { eval("function foo () { yield 42; }; foo();"); });
}

gjstestRun();
