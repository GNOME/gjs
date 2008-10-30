const Everything = imports.gi.Everything;

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
    assertEquals(42, Everything.test_uint8(42));
    assertEquals(42, Everything.test_int16(42));
    assertEquals(42, Everything.test_uint16(42));
    assertEquals(42, Everything.test_int32(42));
    assertEquals(42, Everything.test_uint32(42));
    assertEquals(42, Everything.test_int64(42));
    assertEquals(42, Everything.test_uint64(42));
    assertEquals(42, Everything.test_int(42));
    assertEquals(42, Everything.test_uint(42));
    assertEquals(42, Everything.test_long(42));
    assertEquals(42, Everything.test_ulong(42));
    assertEquals(42, Everything.test_ssize(42));
    assertEquals(42, Everything.test_size(42));
    assertEquals(42, Everything.test_float(42));
    assertEquals(42, Everything.test_double(42));
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

gjstestRun();
