// This used to be called "Everything"

const JSUnit = imports.jsUnit;
const Everything = imports.gi.Regress;
const WarnLib = imports.gi.WarnLib;

// We use Gio to have some objects that we know exist
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
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
    JSUnit.assertEquals(false, Everything.test_boolean(false));
    JSUnit.assertEquals(true, Everything.test_boolean(true));

    JSUnit.assertEquals(42, Everything.test_int8(42));
    JSUnit.assertEquals(-42, Everything.test_int8(-42));

    JSUnit.assertEquals(42, Everything.test_uint8(42));

    JSUnit.assertEquals(42, Everything.test_int16(42));
    JSUnit.assertEquals(-42, Everything.test_int16(-42));

    JSUnit.assertEquals(42, Everything.test_uint16(42));

    JSUnit.assertEquals(42, Everything.test_int32(42));
    JSUnit.assertEquals(-42, Everything.test_int32(-42));

    JSUnit.assertEquals(42, Everything.test_uint32(42));

    JSUnit.assertEquals(42, Everything.test_int64(42));
    JSUnit.assertEquals(-42, Everything.test_int64(-42));

    JSUnit.assertEquals(42, Everything.test_uint64(42));

    JSUnit.assertEquals(42, Everything.test_short(42));
    JSUnit.assertEquals(-42, Everything.test_short(-42));

    JSUnit.assertEquals(42, Everything.test_ushort(42));

    JSUnit.assertEquals(42, Everything.test_int(42));
    JSUnit.assertEquals(-42, Everything.test_int(-42));

    JSUnit.assertEquals(42, Everything.test_uint(42));

    JSUnit.assertEquals(42, Everything.test_long(42));
    JSUnit.assertEquals(-42, Everything.test_long(-42));

    JSUnit.assertEquals(42, Everything.test_ulong(42));

    JSUnit.assertEquals(42, Everything.test_ssize(42));
    JSUnit.assertEquals(-42, Everything.test_ssize(-42));

    JSUnit.assertEquals(42, Everything.test_size(42));

    JSUnit.assertEquals(42, Everything.test_float(42));
    JSUnit.assertEquals(-42, Everything.test_float(-42));

    JSUnit.assertEquals(42, Everything.test_double(42));
    JSUnit.assertEquals(-42, Everything.test_double(-42));

    JSUnit.assertEquals("c", Everything.test_unichar("c"));
    JSUnit.assertEquals("", Everything.test_unichar(""));
    JSUnit.assertEquals("\u2665", Everything.test_unichar("\u2665"));

    let now = Math.floor(new Date().getTime() / 1000);
    let bounced = Math.floor(Everything.test_timet(now));
    JSUnit.assertEquals(bounced, now);
}

function testLimits() {
    JSUnit.assertEquals(UINT8_MAX, Everything.test_uint8(UINT8_MAX));
    JSUnit.assertEquals(UINT16_MAX, Everything.test_uint16(UINT16_MAX));
    JSUnit.assertEquals(UINT32_MAX, Everything.test_uint32(UINT32_MAX));

    // FAIL: expected 18446744073709552000, got 0
    //assertEquals(UINT64_MAX, Everything.test_uint64(UINT64_MAX));

    JSUnit.assertEquals(INT8_MIN, Everything.test_int8(INT8_MIN));
    JSUnit.assertEquals(INT8_MAX, Everything.test_int8(INT8_MAX));
    JSUnit.assertEquals(INT16_MIN, Everything.test_int16(INT16_MIN));
    JSUnit.assertEquals(INT16_MAX, Everything.test_int16(INT16_MAX));
    JSUnit.assertEquals(INT32_MIN, Everything.test_int32(INT32_MIN));
    JSUnit.assertEquals(INT32_MAX, Everything.test_int32(INT32_MAX));
    JSUnit.assertEquals(INT64_MIN, Everything.test_int64(INT64_MIN));

    // FAIL: expected 9223372036854776000, got -9223372036854776000
    //assertEquals(INT64_MAX, Everything.test_int64(INT64_MAX));
}

function testNoImplicitConversionToUnsigned() {
    JSUnit.assertRaises(function() { return Everything.test_uint8(-42); });
    JSUnit.assertRaises(function() { return Everything.test_uint16(-42); });
    JSUnit.assertRaises(function() { return Everything.test_uint32(-42); });

    JSUnit.assertRaises(function() { return Everything.test_uint64(-42); });

    JSUnit.assertRaises(function() { return Everything.test_uint(-42); });
    JSUnit.assertRaises(function() { return Everything.test_size(-42); });
}


function testBadConstructor() {
    try {
        Gio.AppLaunchContext();
    } catch (e) {
        JSUnit.assert(e.message.indexOf("Constructor called as normal method") >= 0);
    }
}

function testStrv() {
    JSUnit.assertTrue(Everything.test_strv_in(['1', '2', '3']));
    // Second two are deliberately not strings
    JSUnit.assertRaises(function() { Everything.test_strv_in(['1', 2, 3]); });

    let strv = Everything.test_strv_out();
    JSUnit.assertEquals(5, strv.length);
    JSUnit.assertEquals("thanks", strv[0]);
    JSUnit.assertEquals("for", strv[1]);
    JSUnit.assertEquals("all", strv[2]);
    JSUnit.assertEquals("the", strv[3]);
    JSUnit.assertEquals("fish", strv[4]);

    strv = Everything.test_strv_out_container();
    JSUnit.assertEquals(3, strv.length);
    JSUnit.assertEquals("1", strv[0]);
    JSUnit.assertEquals("2", strv[1]);
    JSUnit.assertEquals("3", strv[2]);
}

function testInAfterOut() {
    const str = "hello";

    let len = Everything.test_int_out_utf8(str);
    JSUnit.assertEquals("testInAfterOut", str.length, len);
}

function testUtf8() {
    const CONST_STR = "const \u2665 utf8";
    const NONCONST_STR = "nonconst \u2665 utf8";

    JSUnit.assertEquals(CONST_STR, Everything.test_utf8_const_return());
    JSUnit.assertEquals(NONCONST_STR, Everything.test_utf8_nonconst_return());
    Everything.test_utf8_const_in(CONST_STR);
    JSUnit.assertEquals(NONCONST_STR, Everything.test_utf8_out());
    JSUnit.assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
    JSUnit.assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
    JSUnit.assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
    JSUnit.assertEquals(NONCONST_STR, Everything.test_utf8_inout(CONST_STR));
}

function testFilenameReturn() {
    var filenames = Everything.test_filename_return();
    JSUnit.assertEquals(2, filenames.length);
    JSUnit.assertEquals('\u00e5\u00e4\u00f6', filenames[0]);
    JSUnit.assertEquals('/etc/fstab', filenames[1]);
}

function testStaticMeth() {
    let v = Everything.TestObj.new_from_file("/enoent");
    JSUnit.assertTrue(v instanceof Everything.TestObj);
}

function testClosure() {
    let arguments_length = -1;
    let someCallback = function() {
                           arguments_length = arguments.length;
                           return 42;
                       };

    let i = Everything.test_closure(someCallback);

    JSUnit.assertEquals('callback arguments length', 0, arguments_length);
    JSUnit.assertEquals('callback return value', 42, i);
}

function testClosureOneArg() {
    let arguments_length = -1;
    let someCallback = function(someValue) {
                           arguments_length = arguments.length;
                           JSUnit.assertEquals(1, arguments.length);
                           return someValue;
                       };

    let i = Everything.test_closure_one_arg(someCallback, 42);

    JSUnit.assertEquals('callback arguments length', 1, arguments_length);
    JSUnit.assertEquals('callback with one arg return value', 42, i);
}

function testCallback() {
    let callback = function() {
                       return 42;
                   };
    JSUnit.assertEquals('Callback', Everything.test_callback(callback), 42);

    JSUnit.assertEquals('CallbackNull', Everything.test_callback(null), 0);
    JSUnit.assertRaises('CallbackUndefined', function () { Everything.test_callback(undefined); });
}

function testArrayCallback() {
    function arrayEqual(ref, one) {
        JSUnit.assertEquals(ref.length, one.length);
        for (let i = 0; i < ref.length; i++)
            JSUnit.assertEquals(ref[i], one[i]);
    }

    let callback = function(ints, strings) {
        JSUnit.assertEquals(2, arguments.length);

        arrayEqual([-1, 0, 1, 2], ints);
        arrayEqual(["one", "two", "three"], strings);

        return 7;
    };
    JSUnit.assertEquals(Everything.test_array_callback(callback), 14);
    JSUnit.assertRaises(function () { Everything.test_array_callback(null) });
}

function testCallbackDestroyNotify() {
    let testObj = {
        called: 0,
        test: function(data) {
            this.called++;
            return data;
        }
    };
    JSUnit.assertEquals('CallbackDestroyNotify',
                 Everything.test_callback_destroy_notify(Lang.bind(testObj,
                     function() {
                         return testObj.test(42);
                     })), 42);
    JSUnit.assertEquals('CallbackDestroyNotify', testObj.called, 1);
    JSUnit.assertEquals('CallbackDestroyNotify', Everything.test_callback_thaw_notifications(), 42);
}

function testCallbackAsync() {
    let test = function() {
                   return 44;
               };
    Everything.test_callback_async(test);
    let i = Everything.test_callback_thaw_async();
    JSUnit.assertEquals('testCallbackAsyncFinish', 44, i);
}

function testIntValueArg() {
    let i = Everything.test_int_value_arg(42);
    JSUnit.assertEquals('Method taking a GValue', 42, i);
}

function testValueReturn() {
    let i = Everything.test_value_return(42);
    JSUnit.assertEquals('Method returning a GValue', 42, i);
}

/* GList types */
function testGListOut() {
    JSUnit.assertEquals("1,2,3", Everything.test_glist_nothing_return().join(','));
    JSUnit.assertEquals("1,2,3", Everything.test_glist_nothing_return2().join(','));
    JSUnit.assertEquals("1,2,3", Everything.test_glist_container_return().join(','));
    JSUnit.assertEquals("1,2,3", Everything.test_glist_everything_return().join(','));
}
function testGListIn() {
    const STR_LIST = ["1", "2", "3" ];
    Everything.test_glist_nothing_in(STR_LIST);
    Everything.test_glist_nothing_in2(STR_LIST);
    //Everything.test_glist_container_in(STR_LIST);
}

/* GSList types */
function testGSListOut() {
    JSUnit.assertEquals("1,2,3", Everything.test_gslist_nothing_return().join(','));
    JSUnit.assertEquals("1,2,3", Everything.test_gslist_nothing_return2().join(','));
    JSUnit.assertEquals("1,2,3", Everything.test_gslist_container_return().join(','));
    JSUnit.assertEquals("1,2,3", Everything.test_gslist_everything_return().join(','));
}
function testGSListIn() {
    const STR_LIST = ["1", "2", "3" ];
    Everything.test_gslist_nothing_in(STR_LIST);
    Everything.test_gslist_nothing_in2(STR_LIST);
    //Everything.test_gslist_container_in(STR_LIST);
}

/* Array tests */
function testArrayIn() {
    JSUnit.assertEquals(10, Everything.test_array_int_in([1,2,3,4]));
    JSUnit.assertEquals(10, Everything.test_array_gint8_in([1,2,3,4]));
    JSUnit.assertEquals(10, Everything.test_array_gint16_in([1,2,3,4]));
    JSUnit.assertEquals(10, Everything.test_array_gint32_in([1,2,3,4]));
    // FIXME: arrays of int64 are unimplemented
    //assertEquals(10, Everything.test_array_gint64_in([1,2,3,4]));

    // implicit conversions from strings to int arrays
    JSUnit.assertEquals(10, Everything.test_array_gint8_in("\x01\x02\x03\x04"));
    JSUnit.assertEquals(10, Everything.test_array_gint16_in("\x01\x02\x03\x04"));
    JSUnit.assertEquals(2560, Everything.test_array_gint16_in("\u0100\u0200\u0300\u0400"));

    // GType arrays
    JSUnit.assertEquals('[GSimpleAction,GIcon,GBoxed,]',
                 Everything.test_array_gtype_in([Gio.SimpleAction, Gio.Icon, GObject.TYPE_BOXED]));
    JSUnit.assertRaises(function() {
        Everything.test_array_gtype_in(42);
    });
    JSUnit.assertRaises(function() {
        Everything.test_array_gtype_in([undefined]);
    });
    JSUnit.assertRaises(function() {
        // 80 is G_TYPE_OBJECT, but we don't want it to work
        Everything.test_array_gtype_in([80]);
    });
}

function testArrayOut() {
    function arrayEqual(ref, res) {
        JSUnit.assertEquals(ref.length, res.length);
        for (let i = 0; i < ref.length; i++)
            JSUnit.assertEquals(ref[i], res[i]);
    }

    let array =  Everything.test_array_fixed_size_int_out();
    JSUnit.assertEquals(0, array[0]);
    JSUnit.assertEquals(4, array[4]);
    array =  Everything.test_array_fixed_size_int_return();
    JSUnit.assertEquals(0, array[0]);
    JSUnit.assertEquals(4, array[4]);

    array = Everything.test_array_int_none_out();
    arrayEqual([1, 2, 3, 4, 5], array);

    array = Everything.test_array_int_full_out();
    arrayEqual([0, 1, 2, 3, 4], array);

    array = Everything.test_array_int_null_out();
    JSUnit.assertEquals(0, array.length);

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
    JSUnit.assertEquals(null, Everything.test_ghash_null_return());
    JSUnit.assertEquals(HASH_STR, objToString(Everything.test_ghash_nothing_return()));
    JSUnit.assertEquals(HASH_STR, objToString(Everything.test_ghash_nothing_return2()));
    JSUnit.assertEquals(HASH_STR, objToString(Everything.test_ghash_container_return()));
    JSUnit.assertEquals(HASH_STR, objToString(Everything.test_ghash_everything_return()));
}

function testGHashIn() {
    const STR_HASH = { foo: 'bar', baz: 'bat', qux: 'quux' };
    Everything.test_ghash_null_in(null);
    Everything.test_ghash_nothing_in(STR_HASH);
    Everything.test_ghash_nothing_in2(STR_HASH);
}

function testNestedGHashOut() {
    const HASH_STR = '{wibble:{baz:"bat",foo:"bar",qux:"quux"}}';
    JSUnit.assertEquals(HASH_STR, objToString(Everything.test_ghash_nested_everything_return()));
    JSUnit.assertEquals(HASH_STR, objToString(Everything.test_ghash_nested_everything_return2()));
}

/* Enums */
function testEnumParam() {
    let e;

    e = Everything.test_enum_param(Everything.TestEnum.VALUE1);
    JSUnit.assertEquals('Enum parameter', 'value1', e);
    e = Everything.test_enum_param(Everything.TestEnum.VALUE3);
    JSUnit.assertEquals('Enum parameter', 'value3', e);

    e = Everything.test_unsigned_enum_param(Everything.TestEnumUnsigned.VALUE1);
    JSUnit.assertEquals('Enum parameter', 'value1', e);
    e = Everything.test_unsigned_enum_param(Everything.TestEnumUnsigned.VALUE2);
    JSUnit.assertEquals('Enum parameter', 'value2', e);

    JSUnit.assertNotUndefined("Enum $gtype", Everything.TestEnumUnsigned.$gtype);
    JSUnit.assertTrue("Enum $gtype enumerable", "$gtype" in Everything.TestEnumUnsigned);

    JSUnit.assertEquals(Number(Everything.TestError), Everything.TestError.quark());
    JSUnit.assertEquals('value4', Everything.TestEnum.param(Everything.TestEnum.VALUE4));
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
    JSUnit.assertEquals('handler callled', 1, handlerCounter);
    JSUnit.assertEquals('Signal handlers gets called with right object', o, theObject);
    o.emit('test');
    JSUnit.assertEquals('disconnected handler not called', 1, handlerCounter);
}

function testInvalidSignal() {
    let o = new Everything.TestObj();

    JSUnit.assertRaises('connect to invalid signal',
                 function() { o.connect('invalid-signal', function(o) {}); });
    JSUnit.assertRaises('emit invalid signal',
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
    JSUnit.assertEquals('signal handler was passed arg as reference', 44, b.some_int);
}

function testTortureSignature0() {
    let [y, z, q] = Everything.test_torture_signature_0(42, 'foo', 7);
    JSUnit.assertEquals(Math.floor(y), 42);
    JSUnit.assertEquals(z, 84);
    JSUnit.assertEquals(q, 10);
}

function testTortureSignature1Fail() {
    JSUnit.assertRaises(function () {
        let [success, y, z, q] = Everything.test_torture_signature_1(42, 'foo', 7);
    });
}

function testTortureSignature1Success() {
    let [success, y, z, q] = Everything.test_torture_signature_1(11, 'barbaz', 8);
    JSUnit.assertEquals(Math.floor(y), 11);
    JSUnit.assertEquals(z, 22);
    JSUnit.assertEquals(q, 14);
}

function testTortureSignature2() {
    let [y, z, q] = Everything.test_torture_signature_2(42, function () {
        return 0;
    }, 'foo', 7);
    JSUnit.assertEquals(Math.floor(y), 42);
    JSUnit.assertEquals(z, 84);
    JSUnit.assertEquals(q, 10);
}

function testObjTortureSignature0() {
    let o = new Everything.TestObj();
    let [y, z, q] = o.torture_signature_0(42, 'foo', 7);
    JSUnit.assertEquals(Math.floor(y), 42);
    JSUnit.assertEquals(z, 84);
    JSUnit.assertEquals(q, 10);
}

function testObjTortureSignature1Fail() {
    let o = new Everything.TestObj();
    JSUnit.assertRaises(function () {
        let [success, y, z, q] = o.torture_signature_1(42, 'foo', 7);
    });
}

function testObjTortureSignature1Success() {
    let o = new Everything.TestObj();
    let [success, y, z, q] = o.torture_signature_1(11, 'barbaz', 8);
    JSUnit.assertEquals(Math.floor(y), 11);
    JSUnit.assertEquals(z, 22);
    JSUnit.assertEquals(q, 14);
}

function testStrvInGValue() {
    let v = Everything.test_strv_in_gvalue();

    JSUnit.assertEquals(v.length, 3);
    JSUnit.assertEquals(v[0], "one");
    JSUnit.assertEquals(v[1], "two");
    JSUnit.assertEquals(v[2], "three");
}

function testVariant() {
    // Cannot access the variant contents, for now
    let ivar = Everything.test_gvariant_i();
    JSUnit.assertEquals('i', ivar.get_type_string());
    JSUnit.assertTrue(ivar.equal(GLib.Variant.new_int32(1)));

    let svar = Everything.test_gvariant_s();
    JSUnit.assertEquals('s', String.fromCharCode(svar.classify()));
    JSUnit.assertEquals('one', svar.get_string()[0]);

    let asvvar = Everything.test_gvariant_asv();
    JSUnit.assertEquals(2, asvvar.n_children());

    let asvar = Everything.test_gvariant_as();
    let as = asvar.get_strv();
    JSUnit.assertEquals('one', as[0]);
    JSUnit.assertEquals('two', as[1]);
    JSUnit.assertEquals('three', as[2]);
    JSUnit.assertEquals(3, as.length);
}

function testGError() {
    JSUnit.assertEquals(Gio.io_error_quark(), Number(Gio.IOErrorEnum));

    try {
        let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
        file.read(null);
    } catch (x) {
        JSUnit.assertTrue(x instanceof Gio.IOErrorEnum);
        JSUnit.assertTrue(x.matches(Gio.io_error_quark(), Gio.IOErrorEnum.NOT_FOUND));
        JSUnit.assertTrue(x.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_FOUND));

        JSUnit.assertEquals(Gio.io_error_quark(), x.domain);
        JSUnit.assertEquals(Gio.IOErrorEnum.NOT_FOUND, x.code);
    }

    Everything.test_gerror_callback(function(e) {
        JSUnit.assertTrue(e instanceof Gio.IOErrorEnum);
        JSUnit.assertEquals(Gio.io_error_quark(), e.domain);
        JSUnit.assertEquals(Gio.IOErrorEnum.NOT_SUPPORTED, e.code);
        JSUnit.assertEquals('regression test error', e.message);
    });
    Everything.test_owned_gerror_callback(function(e) {
        JSUnit.assertTrue(e instanceof Gio.IOErrorEnum);
        JSUnit.assertEquals(Gio.io_error_quark(), e.domain);
        JSUnit.assertEquals(Gio.IOErrorEnum.PERMISSION_DENIED, e.code);
        JSUnit.assertEquals('regression test owned error', e.message);
    });

    // Calling matches() on an unpaired error used to JSUnit.assert:
    // https://bugzilla.gnome.org/show_bug.cgi?id=689482
    try {
        WarnLib.throw_unpaired();
        JSUnit.assertTrue(false);
    } catch (e) {
        JSUnit.assertFalse(e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_FOUND));
    }
}

function testGErrorMessages() {
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: Gio.IOErrorEnum: *');
    try {
	let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
        file.read(null);
    } catch(e) {
	logError(e);
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    try {
	throw new Gio.IOErrorEnum({ message: 'a message', code: 0 });
    } catch(e) {
	logError(e);
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    logError(new Gio.IOErrorEnum({ message: 'a message', code: 0 }));

    // No stack for GLib.Error constructor
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: Gio.IOErrorEnum: a message');
    logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'));

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: GLib.Error my-error: a message');
    logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'));

    // Now with prefix

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                             'JS ERROR: prefix: Gio.IOErrorEnum: *');
    try {
	let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
        file.read(null);
    } catch(e) {
	logError(e, 'prefix');
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: prefix: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    try {
	throw new Gio.IOErrorEnum({ message: 'a message', code: 0 });
    } catch(e) {
	logError(e, 'prefix');
    }

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: prefix: Gio.IOErrorEnum: a message\ntestGErrorMessages@*');
    logError(new Gio.IOErrorEnum({ message: 'a message', code: 0 }), 'prefix');

    // No stack for GLib.Error constructor
    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: prefix: Gio.IOErrorEnum: a message');
    logError(new GLib.Error(Gio.IOErrorEnum, 0, 'a message'), 'prefix');

    GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
			     'JS ERROR: prefix: GLib.Error my-error: a message');
    logError(new GLib.Error(GLib.quark_from_string('my-error'), 0, 'a message'), 'prefix');
}

function testWrongClassGObject() {
    /* Function calls */
    // Everything.func_obj_null_in expects a Everything.TestObj
    JSUnit.assertRaises(function() {
        Everything.func_obj_null_in(new Gio.SimpleAction);
    });
    JSUnit.assertRaises(function() {
        Everything.func_obj_null_in(new GLib.KeyFile);
    });
    JSUnit.assertRaises(function() {
        Everything.func_obj_null_in(Gio.File.new_for_path('/'));
    });
    Everything.func_obj_null_in(new Everything.TestSubObj);

    /* Method calls */
    JSUnit.assertRaises(function() {
        Everything.TestObj.prototype.instance_method.call(new Gio.SimpleAction);
    });
    JSUnit.assertRaises(function() {
        Everything.TestObj.prototype.instance_method.call(new GLib.KeyFile);
    });
    Everything.TestObj.prototype.instance_method.call(new Everything.TestSubObj);
}

function testWrongClassGBoxed() {
    let simpleBoxed = new Everything.TestSimpleBoxedA;
    // simpleBoxed.equals expects a Everything.TestSimpleBoxedA
    JSUnit.assertRaises(function() {
        simpleBoxed.equals(new Gio.SimpleAction);
    });
    JSUnit.assertRaises(function() {
        simpleBoxed.equals(new Everything.TestObj);
    });
    JSUnit.assertRaises(function() {
        simpleBoxed.equals(new GLib.KeyFile);
    });
    JSUnit.assertTrue(simpleBoxed.equals(simpleBoxed));

    JSUnit.assertRaises(function() {
        Everything.TestSimpleBoxedA.prototype.copy.call(new Gio.SimpleAction);
    });
    JSUnit.assertRaises(function() {
        Everything.TestSimpleBoxedA.prototype.copy.call(new GLib.KeyFile);
    });
    Everything.TestSimpleBoxedA.prototype.copy.call(simpleBoxed);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

