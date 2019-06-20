const ByteArray = imports.byteArray;
const {Gio, GjsPrivate, GLib} = imports.gi;

/* The methods list with their signatures.
 *
 * *** NOTE: If you add stuff here, you need to update the Test class below.
 */
var TestIface = `<node>
<interface name="org.gnome.gjs.Test">
<method name="nonJsonFrobateStuff">
    <arg type="i" direction="in"/>
    <arg type="s" direction="out"/>
</method>
<method name="frobateStuff">
    <arg type="a{sv}" direction="in"/>
    <arg type="a{sv}" direction="out"/>
</method>
<method name="alwaysThrowException">
    <arg type="a{sv}" direction="in"/>
    <arg type="a{sv}" direction="out"/>
</method>
<method name="thisDoesNotExist"/>
<method name="noInParameter">
    <arg type="s" direction="out"/>
</method>
<method name="multipleInArgs">
    <arg type="i" direction="in"/>
    <arg type="i" direction="in"/>
    <arg type="i" direction="in"/>
    <arg type="i" direction="in"/>
    <arg type="i" direction="in"/>
    <arg type="s" direction="out"/>
</method>
<method name="noReturnValue"/>
<method name="emitSignal"/>
<method name="multipleOutValues">
    <arg type="s" direction="out"/>
    <arg type="s" direction="out"/>
    <arg type="s" direction="out"/>
</method>
<method name="oneArrayOut">
    <arg type="as" direction="out"/>
</method>
<method name="arrayOfArrayOut">
    <arg type="aas" direction="out"/>
</method>
<method name="multipleArrayOut">
    <arg type="as" direction="out"/>
    <arg type="as" direction="out"/>
</method>
<method name="arrayOutBadSig">
    <arg type="i" direction="out"/>
</method>
<method name="byteArrayEcho">
    <arg type="ay" direction="in"/>
    <arg type="ay" direction="out"/>
</method>
<method name="byteEcho">
    <arg type="y" direction="in"/>
    <arg type="y" direction="out"/>
</method>
<method name="dictEcho">
    <arg type="a{sv}" direction="in"/>
    <arg type="a{sv}" direction="out"/>
</method>
<method name="echo">
    <arg type="s" direction="in"/>
    <arg type="i" direction="in"/>
    <arg type="s" direction="out"/>
    <arg type="i" direction="out"/>
</method>
<method name="structArray">
    <arg type="a(ii)" direction="out"/>
</method>
<method name="fdIn">
    <arg type="h" direction="in"/>
    <arg type="ay" direction="out"/>
</method>
<method name="fdIn2">
    <arg type="h" direction="in"/>
    <arg type="ay" direction="out"/>
</method>
<method name="fdOut">
    <arg type="ay" direction="in"/>
    <arg type="h" direction="out"/>
</method>
<method name="fdOut2">
    <arg type="ay" direction="in"/>
    <arg type="h" direction="out"/>
</method>
<signal name="signalFoo">
    <arg type="s" direction="out"/>
</signal>
<property name="PropReadOnly" type="d" access="read" />
<property name="PropWriteOnly" type="s" access="write" />
<property name="PropReadWrite" type="v" access="readwrite" />
</interface>
</node>`;

const PROP_READ_ONLY_INITIAL_VALUE = Math.random();
const PROP_READ_WRITE_INITIAL_VALUE = 58;
const PROP_WRITE_ONLY_INITIAL_VALUE = "Initial value";

/* Test is the actual object exporting the dbus methods */
class Test {
    constructor() {
        this._propReadOnly = PROP_READ_ONLY_INITIAL_VALUE;
        this._propWriteOnly = PROP_WRITE_ONLY_INITIAL_VALUE;
        this._propReadWrite = PROP_READ_WRITE_INITIAL_VALUE;

        this._impl = Gio.DBusExportedObject.wrapJSObject(TestIface, this);
        this._impl.export(Gio.DBus.session, '/org/gnome/gjs/Test');
    }

    frobateStuff(args) {
        return { hello: new GLib.Variant('s', 'world') };
    }

    nonJsonFrobateStuff(i) {
        if (i == 42) {
            return "42 it is!";
        } else {
            return "Oops";
        }
    }

    alwaysThrowException() {
        throw Error("Exception!");
    }

    thisDoesNotExist() {
        /* We'll remove this later! */
    }

    noInParameter() {
        return "Yes!";
    }

    multipleInArgs(a, b, c, d, e) {
        return a + " " + b + " " + c + " " + d + " " + e;
    }

    emitSignal() {
        this._impl.emit_signal('signalFoo', GLib.Variant.new('(s)', [ "foobar" ]));
    }

    noReturnValue() {
        /* Empty! */
    }

    /* The following two functions have identical return values
     * in JS, but the bus message will be different.
     * multipleOutValues is "sss", while oneArrayOut is "as"
     */
    multipleOutValues() {
        return [ "Hello", "World", "!" ];
    }

    oneArrayOut() {
        return [ "Hello", "World", "!" ];
    }

    /* Same thing again. In this case multipleArrayOut is "asas",
     * while arrayOfArrayOut is "aas".
     */
    multipleArrayOut() {
        return [[ "Hello", "World" ], [ "World", "Hello" ]];
    }

    arrayOfArrayOut() {
        return [[ "Hello", "World" ], [ "World", "Hello" ]];
    }

    arrayOutBadSig() {
        return Symbol('Hello World!');
    }

    byteArrayEcho(binaryString) {
        return binaryString;
    }

    byteEcho(aByte) {
        return aByte;
    }

    dictEcho(dict) {
        return dict;
    }

    /* This one is implemented asynchronously. Returns
     * the input arguments */
    echoAsync(parameters, invocation) {
        var [someString, someInt] = parameters;
        GLib.idle_add(GLib.PRIORITY_DEFAULT, function() {
            invocation.return_value(new GLib.Variant('(si)', [someString, someInt]));
            return false;
        });
    }

    // double
    get PropReadOnly() {
        return this._propReadOnly;
    }

    // string
    set PropWriteOnly(value) {
        this._propWriteOnly = value;
    }

    // variant
    get PropReadWrite() {
        return new GLib.Variant('s', this._propReadWrite.toString());
    }

    set PropReadWrite(value) {
        this._propReadWrite = value.deep_unpack();
    }

    structArray() {
        return [[128, 123456], [42, 654321]];
    }

    fdIn(fdIndex, fdList) {
        const fd = fdList.get(fdIndex);
        const stream = new Gio.UnixInputStream({fd, closeFd: true});
        const bytes = stream.read_bytes(4096, null);
        return bytes;
    }

    // Same as fdIn(), but implemented asynchronously
    fdIn2Async([fdIndex], invocation, fdList) {
        const fd = fdList.get(fdIndex);
        const stream = new Gio.UnixInputStream({fd, closeFd: true});
        stream.read_bytes_async(4096, GLib.PRIORITY_DEFAULT, null, (obj, res) => {
            const bytes = obj.read_bytes_finish(res);
            invocation.return_value(new GLib.Variant('(ay)', [bytes]));
        });
    }

    fdOut(bytes) {
        const fd = GjsPrivate.open_bytes(bytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        return [0, fdList];
    }

    fdOut2Async([bytes], invocation) {
        GLib.idle_add(GLib.PRIORITY_DEFAULT, function() {
            const fd = GjsPrivate.open_bytes(bytes);
            const fdList = Gio.UnixFDList.new_from_array([fd]);
            invocation.return_value_with_unix_fd_list(new GLib.Variant('(h)', [0]),
                fdList);
            return GLib.SOURCE_REMOVE;
        });
    }
}

const ProxyClass = Gio.DBusProxy.makeProxyWrapper(TestIface);

describe('Exported DBus object', function () {
    var own_name_id;
    var test;
    var proxy;
    let loop;

    let waitForServerProperty = function (property, value = undefined, timeout = 500) {
        let waitId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, timeout, () => {
            waitId = 0;
            throw new Error(`Timeout waiting for property ${property} expired`);
        });

        while (waitId && (!test[property] ||
                          (value !== undefined && test[property] != value)))
            loop.get_context().iteration(true);

        if (waitId)
            GLib.source_remove(waitId);

        expect(waitId).not.toBe(0);
        return test[property];
    };

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
            },
            Gio.DBusProxyFlags.NONE);
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

    it('can call a remote method when not using makeProxyWrapper', function () {
        let info = Gio.DBusNodeInfo.new_for_xml(TestIface);
        let iface = info.interfaces[0];
        let otherProxy = null;
        Gio.DBusProxy.new_for_bus(Gio.BusType.SESSION,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            iface,
            'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test',
            iface.name,
            null,
            (o, res) => {
                otherProxy = Gio.DBusProxy.new_for_bus_finish(res);
                loop.quit();
            });
        loop.run();

        otherProxy.frobateStuffRemote({}, ([result], excp) => {
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
            expect(excp).not.toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can still destructure the return value when an exception is thrown', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Exception in method call: alwaysThrowException: *');

        // This test will not fail, but instead if the functionality is not
        // implemented correctly it will hang. The exception in the function
        // argument destructuring will not propagate across the FFI boundary
        // and the main loop will never quit.
        // https://bugzilla.gnome.org/show_bug.cgi?id=729015
        proxy.alwaysThrowExceptionRemote({}, function([a, b, c], excp) {
            expect(a).not.toBeDefined();
            expect(b).not.toBeDefined();
            expect(c).not.toBeDefined();
            void excp;
            loop.quit();
        });
        loop.run();
    });

    it('throws an exception when trying to call a method that does not exist', function () {
        /* First remove the method from the object! */
        delete Test.prototype.thisDoesNotExist;

        proxy.thisDoesNotExistRemote(function (result, excp) {
            expect(excp).not.toBeNull();
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

    it('handles a bad signature by throwing an exception', function () {
        proxy.arrayOutBadSigRemote(function(result, excp) {
            expect(excp).not.toBeNull();
            loop.quit();
        });
        loop.run();
    });

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

    it('can call a remote method with a Unix FD', function (done) {
        const expectedBytes = ByteArray.fromString('some bytes');
        const fd = GjsPrivate.open_bytes(expectedBytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        proxy.fdInRemote(0, fdList, ([bytes], exc, outFdList) => {
            expect(exc).toBeNull();
            expect(outFdList).toBeNull();
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('can call an asynchronously implemented remote method with a Unix FD', function (done) {
        const expectedBytes = ByteArray.fromString('some bytes');
        const fd = GjsPrivate.open_bytes(expectedBytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        proxy.fdIn2Remote(0, fdList, ([bytes], exc, outFdList) => {
            expect(exc).toBeNull();
            expect(outFdList).toBeNull();
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    function readBytesFromFdSync(fd) {
        const stream = new Gio.UnixInputStream({fd, closeFd: true});
        const bytes = stream.read_bytes(4096, null);
        return ByteArray.fromGBytes(bytes);
    }

    it('can call a remote method that returns a Unix FD', function (done) {
        const expectedBytes = ByteArray.fromString('some bytes');
        proxy.fdOutRemote(expectedBytes, ([fdIndex], exc, outFdList) => {
            expect(exc).toBeNull();
            const bytes = readBytesFromFdSync(outFdList.get(fdIndex));
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('can call an asynchronously implemented remote method that returns a Unix FD', function (done) {
        const expectedBytes = ByteArray.fromString('some bytes');
        proxy.fdOut2Remote(expectedBytes, ([fdIndex], exc, outFdList) => {
            expect(exc).toBeNull();
            const bytes = readBytesFromFdSync(outFdList.get(fdIndex));
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('throws an exception when not passing a Gio.UnixFDList to a method that requires one', function () {
        expect(() => proxy.fdInRemote(0, () => {})).toThrow();
    });

    it('throws an exception when passing a handle out of range of a Gio.UnixFDList', function () {
        const fdList = new Gio.UnixFDList();
        expect(() => proxy.fdInRemote(0, fdList, () => {})).toThrow();
    });

    it('Has defined properties', function () {
        expect(proxy.hasOwnProperty('PropReadWrite')).toBeTruthy();
        expect(proxy.hasOwnProperty('PropReadOnly')).toBeTruthy();
        expect(proxy.hasOwnProperty('PropWriteOnly')).toBeTruthy();
    });

    it('reading readonly property works', function () {
        expect(proxy.PropReadOnly).toEqual(PROP_READ_ONLY_INITIAL_VALUE);
    });

    it('reading readwrite property works', function () {
        expect(proxy.PropReadWrite).toEqual(
            GLib.Variant.new_string(PROP_READ_WRITE_INITIAL_VALUE.toString()));
    });

    it('reading writeonly property returns null', function () {
        expect(proxy.PropWriteOnly).toBeNull();
    });

    it('Setting a readwrite property works', function () {
        let testStr = 'GjsVariantValue';
        expect(() => {
            proxy.PropReadWrite = GLib.Variant.new_string(testStr);
        }).not.toThrow();

        expect(proxy.PropReadWrite.deep_unpack()).toEqual(testStr);

        expect(waitForServerProperty('_propReadWrite', testStr)).toEqual(testStr);
    });

    it('Setting a writeonly property works', function () {
        let testValue = Math.random().toString();
        expect(() => {
            proxy.PropWriteOnly = testValue;
        }).not.toThrow();

        expect(waitForServerProperty('_propWriteOnly', testValue)).toEqual(testValue);
    });

    it('Setting a readonly property does not throw', function () {
        let testValue = Math.random().toString();
        expect(() => {
            proxy.PropReadOnly = testValue;
        }).not.toThrow();
    });
});
