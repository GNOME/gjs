const Everything = imports.gi.Everything;

// We use Gio to have some objects that we know exist
const Gio = imports.gi.Gio;

const INT8_MIN = (-128);
const INT16_MIN = (-32767-1);
const INT32_MIN = (-2147483647-1);
const INT64_MIN = (-9223372036854775807-1);

const INT8_MAX = (127);
const INT16_MAX = (32767);
const INT32_MAX = (2147483647);
const INT64_MAX = (9223372036854775807);

const UINT8_MAX = (255);
const UINT16_MAX = (65535);
const UINT32_MAX = (4294967295);
const UINT64_MAX = (18446744073709551615);

function testLifeUniverseAndEverything() {
    assertEquals(false, Everything.test_boolean(false));
    assertEquals(true, Everything.test_boolean(true));

    assertEquals(42, Everything.test_int8(42));
    assertEquals(-42, Everything.test_int8(-42));

    assertEquals(42, Everything.test_uint8(42));

    assertEquals(42, Everything.test_int16(42));
    assertEquals(-42, Everything.test_int16(-42));

    assertEquals(42, Everything.test_uint16(42));

    assertEquals(42, Everything.test_int32(42));
    assertEquals(-42, Everything.test_int32(-42));

    assertEquals(42, Everything.test_uint32(42));

    assertEquals(42, Everything.test_int64(42));
    assertEquals(-42, Everything.test_int64(-42));

    assertEquals(42, Everything.test_uint64(42));

    assertEquals(42, Everything.test_int(42));
    assertEquals(-42, Everything.test_int(-42));

    assertEquals(42, Everything.test_uint(42));

    assertEquals(42, Everything.test_long(42));
    assertEquals(-42, Everything.test_long(-42));

    assertEquals(42, Everything.test_ulong(42));

    assertEquals(42, Everything.test_ssize(42));
    assertEquals(-42, Everything.test_ssize(-42));

    assertEquals(42, Everything.test_size(42));

    assertEquals(42, Everything.test_float(42));
    assertEquals(-42, Everything.test_float(-42));

    assertEquals(42, Everything.test_double(42));
    assertEquals(-42, Everything.test_double(-42));

    let now = new Date();
    let bounced = Everything.test_timet(now);
    assertEquals(now.getFullYear(), bounced.getFullYear());
    assertEquals(now.getMonth(), bounced.getMonth());
    assertEquals(now.getDay(), bounced.getDay());
    assertEquals(now.getHours(), bounced.getHours());
    assertEquals(now.getSeconds(), bounced.getSeconds());
}

function testLimits() {
    assertEquals(UINT8_MAX, Everything.test_uint8(UINT8_MAX));
    assertEquals(UINT16_MAX, Everything.test_uint16(UINT16_MAX));
    assertEquals(UINT32_MAX, Everything.test_uint32(UINT32_MAX));

    // FAIL: expected 18446744073709552000, got 0
    //assertEquals(UINT64_MAX, Everything.test_uint64(UINT64_MAX));

    assertEquals(INT8_MIN, Everything.test_int8(INT8_MIN));
    assertEquals(INT8_MAX, Everything.test_int8(INT8_MAX));
    assertEquals(INT16_MIN, Everything.test_int16(INT16_MIN));
    assertEquals(INT16_MAX, Everything.test_int16(INT16_MAX));
    assertEquals(INT32_MIN, Everything.test_int32(INT32_MIN));
    assertEquals(INT32_MAX, Everything.test_int32(INT32_MAX));
    assertEquals(INT64_MIN, Everything.test_int64(INT64_MIN));

    // FAIL: expected 9223372036854776000, got -9223372036854776000
    //assertEquals(INT64_MAX, Everything.test_int64(INT64_MAX));
}

function testNoImplicitConversionToUnsigned() {
    assertRaises(function() { return Everything.test_uint8(-42); });
    assertRaises(function() { return Everything.test_uint16(-42); });
    assertRaises(function() { return Everything.test_uint32(-42); });

    assertRaises(function() { return Everything.test_uint64(-42); });

    assertRaises(function() { return Everything.test_uint(-42); });
    assertRaises(function() { return Everything.test_size(-42); });
}


function testBadConstructor() {
    try {
	Gio.AppLaunchContext();
    } catch (e) {
	assert(e.message.indexOf("Constructor called as normal method") >= 0);
    }
}

function testStrv() {
    assertTrue(Everything.test_strv_in(['1', '2', '3']));
    // Second two are deliberately not strings
    assertRaises(function() { Everything.test_strv_in(['1', 2, 3]); });
}

function testFilenameReturn() {
    var filenames = Everything.test_filename_return();
    assertEquals(2, filenames.length);
    assertEquals('\u00e5\u00e4\u00f6', filenames[0]);
    assertEquals('/etc/fstab', filenames[1]);
}

function testStaticMeth() {
    let v = Everything.TestObj.new_from_file("/enoent");
    assertTrue(v instanceof Everything.TestObj);
}

function testClosure() {
    let someCallback = function() {
                           return 42;
                       };

    let i = Everything.test_closure(someCallback);

    assertEquals('callback return value', 42, i);
}

function testClosureOneArg() {
    let someCallback = function(someValue) {
                           return someValue;
                       };

    let i = Everything.test_closure_one_arg(someCallback, 42);

    assertEquals('callback with one arg return value', 42, i);
}

function testIntValueArg() {
    let i = Everything.test_int_value_arg(42);
    assertEquals('Method taking a GValue', 42, i);
}

function testValueReturn() {
    let i = Everything.test_value_return(42);
    assertEquals('Method returning a GValue', 42, i);
}

function testEnumParam() {
   let e = Everything.test_enum_param(Everything.TestEnum.VALUE1);
   assertEquals('Enum parameter', 'value1', e);
}
gjstestRun();
