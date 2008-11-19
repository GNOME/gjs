const Everything = imports.gi.Everything;

function testBoxed() {
  boxed = new Everything.TestBoxed();
  boxed.some_int8 = 42;
  assertEquals(42, boxed.some_int8);
}

gjstestRun();
