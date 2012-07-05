// application/javascript;version=1.8
// This used to be called "Everything"
const Everything = imports.gi.Regress;
if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(window); };
}

// We use Gio to have some objects that we know exist
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const Lang = imports.lang;

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

    assertEquals("c", Everything.test_unichar("c"));
    assertEquals("", Everything.test_unichar(""));
    assertEquals("\u2665", Everything.test_unichar("\u2665"));

    let now = Math.floor(new Date().getTime() / 1000);
    let bounced = Math.floor(Everything.test_timet(now));
    assertEquals(bounced, now);
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

function testInAfterOut() {
    const str = "hello";

    let len = Everything.test_int_out_utf8(str);
    assertEquals("testInAfterOut", str.length, len);
}

function testUtf8() {
    const CONST_STR = "const \u2665 utf8";
    const NONCONST_STR = "nonconst \u2665 utf8";

    assertEquals(CONST_STR, Everything.test_utf8_const_return());
    assertEquals(NONCONST_STR, Everything.test_utf8_nonconst_return());
    Everything.test_utf8_const_in(CONST_STR);
    assertEquals(NONCONST_STR, Everything.test_utf8_out());
    assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
    assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
    assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
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
    let arguments_length = -1;
    let someCallback = function() {
                           arguments_length = arguments.length;
                           return 42;
                       };

    let i = Everything.test_closure(someCallback);

    assertEquals('callback arguments length', 0, arguments_length);
    assertEquals('callback return value', 42, i);
}

function testClosureOneArg() {
    let arguments_length = -1;
    let someCallback = function(someValue) {
                           arguments_length = arguments.length;
                           assertEquals(1, arguments.length);
                           return someValue;
                       };

    let i = Everything.test_closure_one_arg(someCallback, 42);

    assertEquals('callback arguments length', 1, arguments_length);
    assertEquals('callback with one arg return value', 42, i);
}

function testCallback() {
    let callback = function() {
                       return 42;
                   };
    assertEquals('Callback', Everything.test_callback(callback), 42);

    assertEquals('CallbackNull', Everything.test_callback(null), 0);
    assertRaises('CallbackUndefined', function () { Everything.test_callback(undefined) });
}

function testArrayCallback() {
    function arrayEqual(ref, one) {
	assertEquals(ref.length, one.length);
	for (let i = 0; i < ref.length; i++)
	    assertEquals(ref[i], one[i]);
    }

    let callback = function(ints, strings) {
	assertEquals(2, arguments.length);

	arrayEqual([-1, 0, 1, 2], ints);
	arrayEqual(["one", "two", "three"], strings);

	return 7;
    }
    assertEquals(Everything.test_array_callback(callback), 14);
    assertRaises(function () { Everything.test_array_callback(null) });
}

function testCallbackDestroyNotify() {
    let testObj = {
        called: 0,
        test: function(data) {
            this.called++;
            return data;
        }
    };
    assertEquals('CallbackDestroyNotify',
                 Everything.test_callback_destroy_notify(Lang.bind(testObj,
                     function() {
                         return testObj.test(42);
                     })), 42);
    assertEquals('CallbackDestroyNotify', testObj.called, 1);
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
}

/* Array tests */
function testArrayIn() {
    assertEquals(10, Everything.test_array_int_in([1,2,3,4]));
    assertEquals(10, Everything.test_array_gint8_in([1,2,3,4]));
    assertEquals(10, Everything.test_array_gint16_in([1,2,3,4]));
    assertEquals(10, Everything.test_array_gint32_in([1,2,3,4]));
    // FIXME: arrays of int64 are unimplemented
    //assertEquals(10, Everything.test_array_gint64_in([1,2,3,4]));

    // implicit conversions from strings to int arrays
    assertEquals(10, Everything.test_array_gint8_in("\x01\x02\x03\x04"));
    assertEquals(10, Everything.test_array_gint16_in("\x01\x02\x03\x04"));
    assertEquals(2560, Everything.test_array_gint16_in("\u0100\u0200\u0300\u0400"));
}

function testArrayOut() {
    function arrayEqual(ref, res) {
	assertEquals(ref.length, res.length);
	for (let i = 0; i < ref.length; i++)
	    assertEquals(ref[i], res[i]);
    }

    let array =  Everything.test_array_fixed_size_int_out();
    assertEquals(0, array[0]);
    assertEquals(4, array[4]);
    array =  Everything.test_array_fixed_size_int_return();
    assertEquals(0, array[0]);
    assertEquals(4, array[4]);

    array = Everything.test_array_int_none_out();
    arrayEqual([1, 2, 3, 4, 5], array);

    array = Everything.test_array_int_full_out();
    arrayEqual([0, 1, 2, 3, 4], array);

    array = Everything.test_array_int_null_out();
    assertEquals(0, array.length);

    Everything.test_array_int_null_in(null);
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
}

function testNestedGHashOut() {
    const HASH_STR = '{wibble:{baz:"bat",foo:"bar",qux:"quux"}}';
    assertEquals(HASH_STR, objToString(Everything.test_ghash_nested_everything_return()));
    assertEquals(HASH_STR, objToString(Everything.test_ghash_nested_everything_return2()));
}

/* Enums */
function testEnumParam() {
    let e;

    e = Everything.test_enum_param(Everything.TestEnum.VALUE1);
    assertEquals('Enum parameter', 'value1', e);
    e = Everything.test_enum_param(Everything.TestEnum.VALUE3);
    assertEquals('Enum parameter', 'value3', e);

    e = Everything.test_unsigned_enum_param(Everything.TestEnumUnsigned.VALUE1);
    assertEquals('Enum parameter', 'value1', e);
    e = Everything.test_unsigned_enum_param(Everything.TestEnumUnsigned.VALUE2);
    assertEquals('Enum parameter', 'value2', e);

    assertNotUndefined("Enum $gtype", Everything.TestEnumUnsigned.$gtype);
    assertTrue("Enum $gtype enumerable", "$gtype" in Everything.TestEnumUnsigned);
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

function testTortureSignature0() {
    let [y, z, q] = Everything.test_torture_signature_0(42, 'foo', 7);
    assertEquals(Math.floor(y), 42);
    assertEquals(z, 84);
    assertEquals(q, 10);
}

function testTortureSignature1Fail() {
    assertRaises(function () {
        let [success, y, z, q] = Everything.test_torture_signature_1(42, 'foo', 7);
    });
}

function testTortureSignature1Success() {
    let [success, y, z, q] = Everything.test_torture_signature_1(11, 'barbaz', 8);
    assertEquals(Math.floor(y), 11);
    assertEquals(z, 22);
    assertEquals(q, 14);
}

function testTortureSignature2() {
    let [y, z, q] = Everything.test_torture_signature_2(42, function () {
        }, 'foo', 7);
    assertEquals(Math.floor(y), 42);
    assertEquals(z, 84);
    assertEquals(q, 10);
}

function testObjTortureSignature0() {
    let o = new Everything.TestObj();
    let [y, z, q] = o.torture_signature_0(42, 'foo', 7);
    assertEquals(Math.floor(y), 42);
    assertEquals(z, 84);
    assertEquals(q, 10);
}

function testObjTortureSignature1Fail() {
    let o = new Everything.TestObj();
    assertRaises(function () {
        let [success, y, z, q] = o.torture_signature_1(42, 'foo', 7);
    });
}

function testObjTortureSignature1Success() {
    let o = new Everything.TestObj();
    let [success, y, z, q] = o.torture_signature_1(11, 'barbaz', 8);
    assertEquals(Math.floor(y), 11);
    assertEquals(z, 22);
    assertEquals(q, 14);
}

function testStrvInGValue() {
    let v = Everything.test_strv_in_gvalue();

    assertEquals(v.length, 3);
    assertEquals(v[0], "one");
    assertEquals(v[1], "two");
    assertEquals(v[2], "three");
}

function testVariant() {
    // Cannot access the variant contents, for now
    let ivar = Everything.test_gvariant_i();
    assertEquals('i', ivar.get_type_string());
    assertTrue(ivar.equal(GLib.Variant.new_int32(1)));

    let svar = Everything.test_gvariant_s();
    assertEquals('s', String.fromCharCode(svar.classify()));
    assertEquals('one', svar.get_string()[0]);

    let asvvar = Everything.test_gvariant_asv();
    assertEquals(2, asvvar.n_children());

    let asvar = Everything.test_gvariant_as();
    let as = asvar.get_strv();
    assertEquals('one', as[0]);
    assertEquals('two', as[1]);
    assertEquals('three', as[2]);
    assertEquals(3, as.length);
}

function testGError() {
    assertEquals(Gio.io_error_quark(), Number(Gio.IOErrorEnum));

    try {
	let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
	file.read(null);
    } catch (x) {
	assertTrue(x instanceof Gio.IOErrorEnum);
	assertTrue(x.matches(Gio.io_error_quark(), Gio.IOErrorEnum.NOT_FOUND));
	assertTrue(x.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_FOUND));

	assertEquals(Gio.io_error_quark(), x.domain);
	assertEquals(Gio.IOErrorEnum.NOT_FOUND, x.code);
    }

    Everything.test_gerror_callback(function(e) {
	assertTrue(e instanceof Gio.IOErrorEnum);
	assertEquals(Gio.io_error_quark(), e.domain);
	assertEquals(Gio.IOErrorEnum.NOT_SUPPORTED, e.code);
	assertEquals('regression test error', e.message);
    });
    Everything.test_owned_gerror_callback(function(e) {
	assertTrue(e instanceof Gio.IOErrorEnum);
	assertEquals(Gio.io_error_quark(), e.domain);
	assertEquals(Gio.IOErrorEnum.PERMISSION_DENIED, e.code);
	assertEquals('regression test owned error', e.message);
    });
}

function testWrongClassGObject() {
    /* Function calls */
    // Everything.func_obj_null_in expects a Everything.TestObj
    assertRaises(function() {
	Everything.func_obj_null_in(new Gio.SimpleAction);
    });
    assertRaises(function() {
	Everything.func_obj_null_in(new GLib.KeyFile);
    });
    assertRaises(function() {
	Everything.func_obj_null_in(Gio.File.new_for_path('/'));
    });
    Everything.func_obj_null_in(new Everything.TestSubObj);

    /* Method calls */
    assertRaises(function() {
	Everything.TestObj.prototype.instance_method.call(new Gio.SimpleAction);
    });
    assertRaises(function() {
	Everything.TestObj.prototype.instance_method.call(new GLib.KeyFile);
    });
    Everything.TestObj.prototype.instance_method.call(new Everything.TestSubObj);
}

function testWrongClassGBoxed() {
    let simpleBoxed = new Everything.TestSimpleBoxedA;
    // simpleBoxed.equals expects a Everything.TestSimpleBoxedA
    assertRaises(function() {
	simpleBoxed.equals(new Gio.SimpleAction);
    })
    assertRaises(function() {
	simpleBoxed.equals(new Everything.TestObj);
    })
    assertRaises(function() {
	simpleBoxed.equals(new GLib.KeyFile);
    })
    assertTrue(simpleBoxed.equals(simpleBoxed));

    assertRaises(function() {
	Everything.TestSimpleBoxedA.prototype.copy.call(new Gio.SimpleAction);
    });
    assertRaises(function() {
	Everything.TestSimpleBoxedA.prototype.copy.call(new GLib.KeyFile);
    })
    Everything.TestSimpleBoxedA.prototype.copy.call(simpleBoxed);
}

gjstestRun();
