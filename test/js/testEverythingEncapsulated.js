const Everything = imports.gi.Everything;

function testSimpleBoxed() {
    let simple_boxed = new Everything.TestSimpleBoxedA();
    simple_boxed.some_int = 42;
    simple_boxed.some_int8 = 43;
    simple_boxed.some_double = 42.5;
    assertEquals(42, simple_boxed.some_int);
    assertEquals(43, simple_boxed.some_int8);
    assertEquals(42.5, simple_boxed.some_double);
}

function testNestedSimpleBoxed() {
    let simple_boxed = new Everything.TestSimpleBoxedB();
    simple_boxed.some_int8 = 42
    simple_boxed.nested_a.some_int = 43;
    assertEquals(42, simple_boxed.some_int8);
    assertEquals(43, simple_boxed.nested_a.some_int);
}

function testBoxed() {
    let boxed = new Everything.TestBoxed();
    boxed.some_int8 = 42;
    assertEquals(42, boxed.some_int8);
}

gjstestRun();
