const Everything = imports.gi.Everything;
if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(window); };
}

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
    
    assertEquals(42, Everything.test_short(42));
    assertEquals(-42, Everything.test_short(-42));
    
    assertEquals(42, Everything.test_ushort(42));

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

    let strv = Everything.test_strv_out();
    assertEquals(5, strv.length);
    assertEquals("thanks", strv[0]);
    assertEquals("for", strv[1]);
    assertEquals("all", strv[2]);
    assertEquals("the", strv[3]);
    assertEquals("fish", strv[4]);

    strv = Everything.test_strv_out_container();
    assertEquals(3, strv.length);
    assertEquals("1", strv[0]);
    assertEquals("2", strv[1]);
    assertEquals("3", strv[2]);
}

function testUtf8() {
    const CONST_STR = "const \u2665 utf8";
    const NONCONST_STR = "nonconst \u2665 utf8";

    assertEquals(CONST_STR, Everything.test_utf8_const_return());
    assertEquals(NONCONST_STR, Everything.test_utf8_nonconst_return());
    Everything.test_utf8_nonconst_in(NONCONST_STR);
    Everything.test_utf8_const_in(CONST_STR);
    assertEquals(NONCONST_STR, Everything.test_utf8_out());
    assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
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

function testCallback() {
    let callback = function() {
                       return 42;
                   };
    assertEquals('Callback', Everything.test_callback(callback), 42);
}

function testCallbackUserData() {
    let callbackUserData = function(userData) {
                               return userData.foo.length;
                           };

    let userData = {'foo': 'bar'};
    assertEquals('CallbackUserData', Everything.test_callback_user_data(
                                     callbackUserData, userData), userData.foo.length);
}

function testCallbackDestroyNotify() {
    let called = 0;
    let test = function(userData) {
                   called++;
                   return userData;
               };
    assertEquals('CallbackDestroyNotify', Everything.test_callback_destroy_notify(test, 42), 42);
    assertEquals('CallbackDestroyNotify', called, 1);
    assertEquals('CallbackDestroyNotify', Everything.test_callback_thaw_notifications(), 42);
}

function testCallbackAsync() {
    let test = function(userData) {
                   return 44;
               };
    Everything.test_callback_async(test, 44);
    let i = Everything.test_callback_thaw_async();
    assertEquals('testCallbackAsyncFinish', 44, i);
}

function testIntValueArg() {
    let i = Everything.test_int_value_arg(42);
    assertEquals('Method taking a GValue', 42, i);
}

function testValueReturn() {
    let i = Everything.test_value_return(42);
    assertEquals('Method returning a GValue', 42, i);
}

/* GList types */
function testGListOut() {
    assertEquals("1,2,3", Everything.test_glist_nothing_return().join(','));
    assertEquals("1,2,3", Everything.test_glist_nothing_return2().join(','));
    assertEquals("1,2,3", Everything.test_glist_container_return().join(','));
    assertEquals("1,2,3", Everything.test_glist_everything_return().join(','));
}
function testGListIn() {
    const STR_LIST = ["1", "2", "3" ];
    Everything.test_glist_nothing_in(STR_LIST);
    Everything.test_glist_nothing_in2(STR_LIST);
    //Everything.test_glist_container_in(STR_LIST);
    Everything.test_glist_everything_in(STR_LIST);
}

/* GSList types */
function testGSListOut() {
    assertEquals("1,2,3", Everything.test_gslist_nothing_return().join(','));
    assertEquals("1,2,3", Everything.test_gslist_nothing_return2().join(','));
    assertEquals("1,2,3", Everything.test_gslist_container_return().join(','));
    assertEquals("1,2,3", Everything.test_gslist_everything_return().join(','));
}
function testGSListIn() {
    const STR_LIST = ["1", "2", "3" ];
    Everything.test_gslist_nothing_in(STR_LIST);
    Everything.test_gslist_nothing_in2(STR_LIST);
    //Everything.test_gslist_container_in(STR_LIST);
    Everything.test_gslist_everything_in(STR_LIST);
}

/* Array tests */
function testArrayIn() {
    assertEquals(10, Everything.test_array_int_in(4, [1,2,3,4]));
    assertEquals(10, Everything.test_array_gint8_in(4, [1,2,3,4]));
    assertEquals(10, Everything.test_array_gint16_in(4, [1,2,3,4]));
    assertEquals(10, Everything.test_array_gint32_in(4, [1,2,3,4]));
    // FIXME: arrays of int64 are unimplemented
    //assertEquals(10, Everything.test_array_gint64_in(4, [1,2,3,4]));

    // implicit conversions from strings to int arrays
    assertEquals(10, Everything.test_array_gint8_in(4, "\x01\x02\x03\x04"));
    assertEquals(10, Everything.test_array_gint16_in(4, "\x01\x02\x03\x04"));
    assertEquals(2560, Everything.test_array_gint16_in(4, "\u0100\u0200\u0300\u0400"));
}

function testArrayOut() {
    // FIXME: test_array_int_full_out and test_array_int_none_out unimplemented
}

/* GHash type */

// Convert an object to a predictable (not-hash-order-dependent) string
function objToString(v) {
    if (typeof(v) == "object") {
	let keys = [];
	for (let k in v)
	    keys.push(k);
	keys.sort();
	return "{" + keys.map(function(k) {
	    return k + ":" + objToString(v[k]);
	}) + "}";
    } else if (typeof(v) == "string") {
	return '"' + v + '"';
    } else {
	return v;
    }
}

function testGHashOut() {
    const HASH_STR = '{baz:"bat",foo:"bar",qux:"quux"}';
    assertEquals(null, Everything.test_ghash_null_return());
    assertEquals(HASH_STR, objToString(Everything.test_ghash_nothing_return()));
    assertEquals(HASH_STR, objToString(Everything.test_ghash_nothing_return2()));
    assertEquals(HASH_STR, objToString(Everything.test_ghash_container_return()));
    assertEquals(HASH_STR, objToString(Everything.test_ghash_everything_return()));
}

function testGHashIn() {
    const STR_HASH = { foo: 'bar', baz: 'bat', qux: 'quux' };
    Everything.test_ghash_null_in(null);
    Everything.test_ghash_nothing_in(STR_HASH);
    Everything.test_ghash_nothing_in2(STR_HASH);
    //Everything.test_ghash_container_in(STR_HASH);
    Everything.test_ghash_everything_in(STR_HASH);
}

function testNestedGHashOut() {
    const HASH_STR = '{wibble:{baz:"bat",foo:"bar",qux:"quux"}}';
    assertEquals(HASH_STR, objToString(Everything.test_ghash_nested_everything_return()));
    assertEquals(HASH_STR, objToString(Everything.test_ghash_nested_everything_return2()));
}

/* Enums */
function testEnumParam() {
   let e = Everything.test_enum_param(Everything.TestEnum.VALUE1);
   assertEquals('Enum parameter', 'value1', e);
}

function testSignal() {
    let handlerCounter = 0;
    let o = new Everything.TestObj();
    let theObject = null;

    let handlerId = o.connect('test', function(signalObject) {
                                          handlerCounter ++;
                                          theObject = signalObject;
                                          o.disconnect(handlerId);
                                      });

    o.emit('test');
    assertEquals('handler callled', 1, handlerCounter);
    assertEquals('Signal handlers gets called with right object', o, theObject);
    o.emit('test');
    assertEquals('disconnected handler not called', 1, handlerCounter);
}

function testInvalidSignal() {
    let o = new Everything.TestObj();

    assertRaises('connect to invalid signal',
                 function() { o.connect('invalid-signal', function(o) {}); });
    assertRaises('emit invalid signal',
                 function() { o.emit('invalid-signal'); });
}

function testSignalWithStaticScopeArg() {
    let o = new Everything.TestObj();
    let b = new Everything.TestSimpleBoxedA({ some_int: 42,
                                              some_int8: 43,
                                              some_double: 42.5,
                                              some_enum: Everything.TestEnum.VALUE3 });

    o.connect('test-with-static-scope-arg', function(signalObject, signalArg) {
                                                signalArg.some_int = 44;
                                            });

    o.emit('test-with-static-scope-arg', b);
    assertEquals('signal handler was passed arg as reference', 44, b.some_int);
}

gjstestRun();
