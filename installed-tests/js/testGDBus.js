const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;

/* The methods list with their signatures.
 *
 * *** NOTE: If you add stuff here, you need to update testIntrospectReal
 */
var TestIface = '<node> \
<interface name="org.gnome.gjs.Test"> \
<method name="nonJsonFrobateStuff"> \
    <arg type="i" direction="in"/> \
    <arg type="s" direction="out"/> \
</method> \
<method name="frobateStuff"> \
    <arg type="a{sv}" direction="in"/> \
    <arg type="a{sv}" direction="out"/> \
</method> \
<method name="alwaysThrowException"> \
    <arg type="a{sv}" direction="in"/> \
    <arg type="a{sv}" direction="out"/> \
</method> \
<method name="thisDoesNotExist"/> \
<method name="noInParameter"> \
    <arg type="s" direction="out"/> \
</method> \
<method name="multipleInArgs"> \
    <arg type="i" direction="in"/> \
    <arg type="i" direction="in"/> \
    <arg type="i" direction="in"/> \
    <arg type="i" direction="in"/> \
    <arg type="i" direction="in"/> \
    <arg type="s" direction="out"/> \
</method> \
<method name="noReturnValue"/> \
<method name="emitSignal"/> \
<method name="multipleOutValues"> \
    <arg type="s" direction="out"/> \
    <arg type="s" direction="out"/> \
    <arg type="s" direction="out"/> \
</method> \
<method name="oneArrayOut"> \
    <arg type="as" direction="out"/> \
</method> \
<method name="arrayOfArrayOut"> \
    <arg type="aas" direction="out"/> \
</method> \
<method name="multipleArrayOut"> \
    <arg type="as" direction="out"/> \
    <arg type="as" direction="out"/> \
</method> \
<method name="arrayOutBadSig"> \
    <arg type="i" direction="out"/> \
</method> \
<method name="byteArrayEcho"> \
    <arg type="ay" direction="in"/> \
    <arg type="ay" direction="out"/> \
</method> \
<method name="byteEcho"> \
    <arg type="y" direction="in"/> \
    <arg type="y" direction="out"/> \
</method> \
<method name="dictEcho"> \
    <arg type="a{sv}" direction="in"/> \
    <arg type="a{sv}" direction="out"/> \
</method> \
<method name="echo"> \
    <arg type="s" direction="in"/> \
    <arg type="i" direction="in"/> \
    <arg type="s" direction="out"/> \
    <arg type="i" direction="out"/> \
</method> \
<method name="structArray"> \
    <arg type="a(ii)" direction="out"/> \
</method> \
<signal name="signalFoo"> \
    <arg type="s" direction="out"/> \
</signal> \
<property name="PropReadOnly" type="b" access="read" /> \
<property name="PropWriteOnly" type="s" access="write" /> \
<property name="PropReadWrite" type="v" access="readwrite" /> \
</interface> \
</node>';

/* Test is the actual object exporting the dbus methods */
function Test() {
    this._init();
}

const PROP_READ_WRITE_INITIAL_VALUE = 58;
const PROP_WRITE_ONLY_INITIAL_VALUE = "Initial value";

Test.prototype = {
    _init: function(){
        this._propWriteOnly = PROP_WRITE_ONLY_INITIAL_VALUE;
        this._propReadWrite = PROP_READ_WRITE_INITIAL_VALUE;

        this._impl = Gio.DBusExportedObject.wrapJSObject(TestIface, this);
        this._impl.export(Gio.DBus.session, '/org/gnome/gjs/Test');
    },

    frobateStuff: function(args) {
        return { hello: new GLib.Variant('s', 'world') };
    },

    nonJsonFrobateStuff: function(i) {
        if (i == 42) {
            return "42 it is!";
        } else {
            return "Oops";
        }
    },

    alwaysThrowException: function() {
        throw Error("Exception!");
    },

    thisDoesNotExist: function () {
        /* We'll remove this later! */
    },

    noInParameter: function() {
        return "Yes!";
    },

    multipleInArgs: function(a, b, c, d, e) {
        return a + " " + b + " " + c + " " + d + " " + e;
    },

    emitSignal: function() {
        this._impl.emit_signal('signalFoo', GLib.Variant.new('(s)', [ "foobar" ]));
    },

    noReturnValue: function() {
        /* Empty! */
    },

    /* The following two functions have identical return values
     * in JS, but the bus message will be different.
     * multipleOutValues is "sss", while oneArrayOut is "as"
     */
    multipleOutValues: function() {
        return [ "Hello", "World", "!" ];
    },

    oneArrayOut: function() {
        return [ "Hello", "World", "!" ];
    },

    /* Same thing again. In this case multipleArrayOut is "asas",
     * while arrayOfArrayOut is "aas".
     */
    multipleArrayOut: function() {
        return [[ "Hello", "World" ], [ "World", "Hello" ]];
    },

    arrayOfArrayOut: function() {
        return [[ "Hello", "World" ], [ "World", "Hello" ]];
    },

    arrayOutBadSig: function() {
        return [ "Hello", "World", "!" ];
    },

    byteArrayEcho: function(binaryString) {
        return binaryString;
    },

    byteEcho: function(aByte) {
        return aByte;
    },

    dictEcho: function(dict) {
        return dict;
    },

    /* This one is implemented asynchronously. Returns
     * the input arguments */
    echoAsync: function(parameters, invocation) {
        var [someString, someInt] = parameters;
        GLib.idle_add(GLib.PRIORITY_DEFAULT, function() {
            invocation.return_value(new GLib.Variant('(si)', [someString, someInt]));
            return false;
        });
    },

    // boolean
    get PropReadOnly() {
        return true;
    },

    // string
    set PropWriteOnly(value) {
        this._propWriteOnly = value;
    },

    // variant
    get PropReadWrite() {
        return new GLib.Variant('s', this._propReadWrite.toString());
    },

    set PropReadWrite(value) {
        this._propReadWrite = value.deep_unpack();
    },

    structArray: function () {
        return [[128, 123456], [42, 654321]];
    }
};

const ProxyClass = Gio.DBusProxy.makeProxyWrapper(TestIface);

describe('Exported DBus object', function () {
    var own_name_id;
    var test;
    var proxy;
    let loop;

    beforeAll(function () {
        loop = new GLib.MainLoop(null, false);

        test = new Test();
        own_name_id = Gio.DBus.session.own_name('org.gnome.gjs.Test',
            Gio.BusNameOwnerFlags.NONE,
            name => {
                log("Acquired name " + name);
                loop.quit();
            },
            name => {
                log("Lost name " + name);
            });
        loop.run();
        new ProxyClass(Gio.DBus.session, 'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test',
            (obj, error) => {
                expect(error).toBeNull();
                proxy = obj;
                expect(proxy).not.toBeNull();
                loop.quit();
            });
        loop.run();
    });

    afterAll(function () {
        // Not really needed, but if we don't cleanup
        // memory checking will complain
        Gio.DBus.session.unown_name(own_name_id);
    });

    beforeEach(function () {
        loop = new GLib.MainLoop(null, false);
    });

    it('can call a remote method', function () {
        proxy.frobateStuffRemote({}, ([result], excp) => {
            expect(excp).toBeNull();
            expect(result.hello.deep_unpack()).toEqual('world');
            loop.quit();
        });
        loop.run();
    });

    /* excp must be exactly the exception thrown by the remote method
       (more or less) */
    it('can handle an exception thrown by a remote method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Exception in method call: alwaysThrowException: *');

        proxy.alwaysThrowExceptionRemote({}, function(result, excp) {
            expect(result).toBeNull();
            expect(excp).not.toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('throws an exception when trying to call a method that does not exist', function () {
        /* First remove the method from the object! */
        delete Test.prototype.thisDoesNotExist;

        proxy.thisDoesNotExistRemote(function (result, excp) {
            expect(excp).not.toBeNull();
            expect(result).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can pass a parameter to a remote method that is not a JSON object', function () {
        proxy.nonJsonFrobateStuffRemote(42, ([result], excp) => {
            expect(result).toEqual('42 it is!');
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call a remote method with no in parameter', function () {
        proxy.noInParameterRemote(([result], excp) => {
            expect(result).toEqual('Yes!');
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call a remote method with multiple in parameters', function () {
        proxy.multipleInArgsRemote(1, 2, 3, 4, 5, ([result], excp) => {
            expect(result).toEqual('1 2 3 4 5');
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call a remote method with no return value', function () {
        proxy.noReturnValueRemote(([result], excp) => {
            expect(result).not.toBeDefined();
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can emit a DBus signal', function () {
        let handler = jasmine.createSpy('signalFoo');
        let id = proxy.connectSignal('signalFoo', handler);
        handler.and.callFake(() => proxy.disconnectSignal(id));

        proxy.emitSignalRemote(([result], excp) => {
            expect(result).not.toBeDefined();
            expect(excp).toBeNull();
            expect(handler).toHaveBeenCalledTimes(1);
            expect(handler).toHaveBeenCalledWith(jasmine.anything(),
                jasmine.anything(), ['foobar']);
            loop.quit();
        });
        loop.run();
    });

    it('can call a remote method with multiple return values', function () {
        proxy.multipleOutValuesRemote(function(result, excp) {
            expect(result).toEqual(['Hello', 'World', '!']);
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('does not coalesce one array into the array of return values', function () {
        proxy.oneArrayOutRemote(([result], excp) => {
            expect(result).toEqual(['Hello', 'World', '!']);
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('does not coalesce an array of arrays into the array of return values', function () {
        proxy.arrayOfArrayOutRemote(([[a1, a2]], excp) => {
            expect(a1).toEqual(['Hello', 'World']);
            expect(a2).toEqual(['World', 'Hello']);
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can return multiple arrays from a remote method', function () {
        proxy.multipleArrayOutRemote(([a1, a2], excp) => {
            expect(a1).toEqual(['Hello', 'World']);
            expect(a2).toEqual(['World', 'Hello']);
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    /* COMPAT: This test should test what happens when a TypeError is thrown
     * during argument marshalling, but conversions don't throw TypeErrors
     * anymore, so we can't test that ... until we upgrade to mozjs38 which has
     * Symbols. Converting a Symbol to an int32 or string will throw a TypeError.
     */
    xit('handles a bad signature by throwing an exception', function () {
        proxy.arrayOutBadSigRemote(function(result, excp) {
            expect(result).toBeNull();
            expect(excp).not.toBeNull();
            loop.quit();
        });
        loop.run();
    }).pend('currently cannot throw TypeError during conversion');

    it('can call a remote method that is implemented asynchronously', function () {
        let someString = "Hello world!";
        let someInt = 42;

        proxy.echoRemote(someString, someInt,
            function(result, excp) {
                expect(excp).toBeNull();
                expect(result).toEqual([someString, someInt]);
                loop.quit();
            });
        loop.run();
    });

    it('can send and receive bytes from a remote method', function () {
        let loop = GLib.MainLoop.new(null, false);

        let someBytes = [ 0, 63, 234 ];
        someBytes.forEach(b => {
            proxy.byteEchoRemote(b, ([result], excp) => {
                expect(excp).toBeNull();
                expect(result).toEqual(b);
                loop.quit();
            });

            loop.run();
        });
    });

    it('can call a remote method that returns an array of structs', function () {
        proxy.structArrayRemote(([result], excp) => {
            expect(excp).toBeNull();
            expect(result).toEqual([[128, 123456], [42, 654321]]);
            loop.quit();
        });
        loop.run();
    });

    it('can send and receive dicts from a remote method', function () {
        let someDict = {
            aDouble: new GLib.Variant('d', 10),
            // should be an integer after round trip
            anInteger: new GLib.Variant('i', 10.5),
            // should remain a double
            aDoubleBeforeAndAfter: new GLib.Variant('d', 10.5),
        };

        proxy.dictEchoRemote(someDict, ([result], excp) => {
            expect(excp).toBeNull();
            expect(result).not.toBeNull();

            // verify the fractional part was dropped off int
            expect(result['anInteger'].deep_unpack()).toEqual(10);

            // and not dropped off a double
            expect(result['aDoubleBeforeAndAfter'].deep_unpack()).toEqual(10.5);

            // check without type conversion
            expect(result['aDouble'].deep_unpack()).toBe(10.0);

            loop.quit();
        });
        loop.run();
    });
});
