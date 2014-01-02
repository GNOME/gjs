const JSUnit = imports.jsUnit;

var someUndefined;
var someNumber = 1;
var someOtherNumber = 42;
var someString = "hello";
var someOtherString = "world";

JSUnit.assert(true);
JSUnit.assertTrue(true);
JSUnit.assertFalse(false);

JSUnit.assertEquals(someNumber, someNumber);
JSUnit.assertEquals(someString, someString);

JSUnit.assertNotEquals(someNumber, someOtherNumber);
JSUnit.assertNotEquals(someString, someOtherString);

JSUnit.assertNull(null);
JSUnit.assertNotNull(someNumber);
JSUnit.assertUndefined(someUndefined);
JSUnit.assertNotUndefined(someNumber);
JSUnit.assertNaN(0/0);
JSUnit.assertNotNaN(someNumber);

// test assertRaises()
JSUnit.assertRaises(function() { throw new Object(); });
try {   // calling assertRaises with non-function is an error, not assertion failure
    JSUnit.assertRaises(true);
} catch(e) {
    JSUnit.assertUndefined(e.isJsUnitException);
}
try {   // function not throwing an exception is assertion failure
    JSUnit.assertRaises(function() { return true; });
} catch(e) {
    JSUnit.assertTrue(e.isJsUnitException);
}
