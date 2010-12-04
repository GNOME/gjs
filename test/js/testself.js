// application/javascript;version=1.8
var someUndefined;
var someNumber = 1;
var someOtherNumber = 42;
var someString = "hello";
var someOtherString = "world";

assert(true);
assertTrue(true);
assertFalse(false);

assertEquals(someNumber, someNumber);
assertEquals(someString, someString);

assertNotEquals(someNumber, someOtherNumber);
assertNotEquals(someString, someOtherString);

assertNull(null);
assertNotNull(someNumber);
assertUndefined(someUndefined);
assertNotUndefined(someNumber);
assertNaN(0/0);
assertNotNaN(someNumber);

// test assertRaises()
assertRaises(function() { throw new Object(); });
try {   // calling assertRaises with non-function is an error, not assertion failure
    assertRaises(true);
} catch(e) {
    assertUndefined(e.isJsUnitException);
}
try {   // function not throwing an exception is assertion failure
    assertRaises(function() { return true; });
} catch(e) {
    assertTrue(e.isJsUnitException);
}
