// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

import Gio from 'gi://Gio';
import GjsTestTools from 'gi://GjsTestTools';
import GLib from 'gi://GLib';
import GioUnix from 'gi://GioUnix';

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
<property name="PropPrePacked" type="a{sv}" access="read" />
</interface>
</node>`;

const PROP_READ_ONLY_INITIAL_VALUE = Math.random();
const PROP_READ_WRITE_INITIAL_VALUE = 58;
const PROP_WRITE_ONLY_INITIAL_VALUE = 'Initial value';

/* Test is the actual object exporting the dbus methods */
class Test {
    constructor() {
        this._propReadOnly = PROP_READ_ONLY_INITIAL_VALUE;
        this._propWriteOnly = PROP_WRITE_ONLY_INITIAL_VALUE;
        this._propReadWrite = PROP_READ_WRITE_INITIAL_VALUE;

        this._impl = Gio.DBusExportedObject.wrapJSObject(TestIface, this);
        this._impl.export(Gio.DBus.session, '/org/gnome/gjs/Test');
    }

    frobateStuff() {
        return {hello: new GLib.Variant('s', 'world')};
    }

    nonJsonFrobateStuff(i) {
        if (i === 42)
            return '42 it is!';
        else
            return 'Oops';
    }

    alwaysThrowException() {
        throw Error('Exception!');
    }

    thisDoesNotExist() {
        /* We'll remove this later! */
    }

    noInParameter() {
        return 'Yes!';
    }

    multipleInArgs(a, b, c, d, e) {
        return `${a} ${b} ${c} ${d} ${e}`;
    }

    emitPropertyChanged(name, value) {
        this._impl.emit_property_changed(name, value);
    }

    emitSignal() {
        this._impl.emit_signal('signalFoo', GLib.Variant.new('(s)', ['foobar']));
    }

    noReturnValue() {
        /* Empty! */
    }

    /* The following two functions have identical return values
     * in JS, but the bus message will be different.
     * multipleOutValues is "sss", while oneArrayOut is "as"
     */
    multipleOutValues() {
        return ['Hello', 'World', '!'];
    }

    oneArrayOut() {
        return ['Hello', 'World', '!'];
    }

    /* Same thing again. In this case multipleArrayOut is "asas",
     * while arrayOfArrayOut is "aas".
     */
    multipleArrayOut() {
        return [['Hello', 'World'], ['World', 'Hello']];
    }

    arrayOfArrayOut() {
        return [['Hello', 'World'], ['World', 'Hello']];
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
        GLib.idle_add(GLib.PRIORITY_DEFAULT, function () {
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
        this._propReadWrite = value.deepUnpack();
    }

    get PropPrePacked() {
        return new GLib.Variant('a{sv}', {
            member: GLib.Variant.new_string('value'),
        });
    }

    structArray() {
        return [[128, 123456], [42, 654321]];
    }

    fdIn(fdIndex, fdList) {
        const fd = fdList.get(fdIndex);
        const stream = new GioUnix.InputStream({fd, closeFd: true});
        const bytes = stream.read_bytes(4096, null);
        return bytes;
    }

    // Same as fdIn(), but implemented asynchronously
    fdIn2Async([fdIndex], invocation, fdList) {
        const fd = fdList.get(fdIndex);
        const stream = new GioUnix.InputStream({fd, closeFd: true});
        stream.read_bytes_async(4096, GLib.PRIORITY_DEFAULT, null, (obj, res) => {
            const bytes = obj.read_bytes_finish(res);
            invocation.return_value(new GLib.Variant('(ay)', [bytes]));
        });
    }

    fdOut(bytes) {
        const fd = GjsTestTools.open_bytes(bytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        return [0, fdList];
    }

    fdOut2Async([bytes], invocation) {
        GLib.idle_add(GLib.PRIORITY_DEFAULT, function () {
            const fd = GjsTestTools.open_bytes(bytes);
            const fdList = Gio.UnixFDList.new_from_array([fd]);
            invocation.return_value_with_unix_fd_list(new GLib.Variant('(h)', [0]),
                fdList);
            return GLib.SOURCE_REMOVE;
        });
    }
}

const ProxyClass = Gio.DBusProxy.makeProxyWrapper(TestIface);

describe('Exported DBus object', function () {
    let ownNameID;
    var test;
    var proxy;
    let loop;
    const expectedBytes = new TextEncoder().encode('some bytes');

    function waitForServerProperty(property, value = undefined, timeout = 500) {
        let waitId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, timeout, () => {
            waitId = 0;
            throw new Error(`Timeout waiting for property ${property} expired`);
        });

        while (waitId && (!test[property] ||
                          value !== undefined && test[property] !== value))
            loop.get_context().iteration(true);

        if (waitId)
            GLib.source_remove(waitId);

        expect(waitId).not.toBe(0);
        return test[property];
    }

    beforeAll(function () {
        loop = new GLib.MainLoop(null, false);

        test = new Test();
        ownNameID = Gio.DBus.session.own_name('org.gnome.gjs.Test',
            Gio.BusNameOwnerFlags.NONE,
            name => {
                log(`Acquired name ${name}`);
                loop.quit();
            },
            name => {
                log(`Lost name ${name}`);
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
        Gio.DBus.session.unown_name(ownNameID);
    });

    beforeEach(function () {
        loop = new GLib.MainLoop(null, false);
    });

    it('can call a remote method', function () {
        proxy.frobateStuffRemote({}, ([result], excp) => {
            expect(excp).toBeNull();
            expect(result.hello.deepUnpack()).toEqual('world');
            loop.quit();
        });
        loop.run();
    });

    it('can call a method with async/await', async function () {
        const [{hello}] = await proxy.frobateStuffAsync({});
        expect(hello.deepUnpack()).toEqual('world');
    });

    it('can initiate a proxy with promise and call a method with async/await', async function () {
        const asyncProxy = await ProxyClass.newAsync(Gio.DBus.session,
            'org.gnome.gjs.Test', '/org/gnome/gjs/Test');
        expect(asyncProxy).toBeInstanceOf(Gio.DBusProxy);
        const [{hello}] = await asyncProxy.frobateStuffAsync({});
        expect(hello.deepUnpack()).toEqual('world');
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
            expect(result.hello.deepUnpack()).toEqual('world');
            loop.quit();
        });
        loop.run();
    });

    /* excp must be exactly the exception thrown by the remote method
       (more or less) */
    it('can handle an exception thrown by a remote method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Exception in method call: alwaysThrowException: *');

        proxy.alwaysThrowExceptionRemote({}, function (result, excp) {
            expect(excp).not.toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can handle an exception thrown by a method with async/await', async function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Exception in method call: alwaysThrowException: *');

        await expectAsync(proxy.alwaysThrowExceptionAsync({})).toBeRejected();
    });

    it('can still destructure the return value when an exception is thrown', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            'JS ERROR: Exception in method call: alwaysThrowException: *');

        // This test will not fail, but instead if the functionality is not
        // implemented correctly it will hang. The exception in the function
        // argument destructuring will not propagate across the FFI boundary
        // and the main loop will never quit.
        // https://bugzilla.gnome.org/show_bug.cgi?id=729015
        proxy.alwaysThrowExceptionRemote({}, function ([a, b, c], excp) {
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

    it('throws an exception when trying to call an async method that does not exist', async function () {
        delete Test.prototype.thisDoesNotExist;
        await expectAsync(proxy.thisDoesNotExistAsync()).toBeRejected();
    });

    it('can pass a parameter to a remote method that is not a JSON object', function () {
        proxy.nonJsonFrobateStuffRemote(42, ([result], excp) => {
            expect(result).toEqual('42 it is!');
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can pass a parameter to a method with async/await that is not a JSON object', async function () {
        const [result] = await proxy.nonJsonFrobateStuffAsync(1);
        expect(result).toEqual('Oops');
    });

    it('can call a remote method with no in parameter', function () {
        proxy.noInParameterRemote(([result], excp) => {
            expect(result).toEqual('Yes!');
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call an async/await method with no in parameter', async function () {
        const [result] = await proxy.noInParameterAsync();
        expect(result).toEqual('Yes!');
    });

    it('can call a remote method with multiple in parameters', function () {
        proxy.multipleInArgsRemote(1, 2, 3, 4, 5, ([result], excp) => {
            expect(result).toEqual('1 2 3 4 5');
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call an async/await method with multiple in parameters', async function () {
        const [result] = await proxy.multipleInArgsAsync(1, 2, 3, 4, 5);
        expect(result).toEqual('1 2 3 4 5');
    });

    it('can call a remote method with no return value', function () {
        proxy.noReturnValueRemote(([result], excp) => {
            expect(result).not.toBeDefined();
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call an async/await method with no return value', async function () {
        const [result] = await proxy.noReturnValueAsync();
        expect(result).not.toBeDefined();
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

    it('can emit a DBus signal with async/await', async function () {
        const handler = jasmine.createSpy('signalFoo');
        const id = proxy.connectSignal('signalFoo', handler);
        handler.and.callFake(() => proxy.disconnectSignal(id));

        const [result] = await proxy.emitSignalAsync();
        expect(result).not.toBeDefined();
        expect(handler).toHaveBeenCalledTimes(1);
        expect(handler).toHaveBeenCalledWith(jasmine.anything(),
            jasmine.anything(), ['foobar']);
    });

    it('can call a remote method with multiple return values', function () {
        proxy.multipleOutValuesRemote(function (result, excp) {
            expect(result).toEqual(['Hello', 'World', '!']);
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('can call an async/await method with multiple return values', async function () {
        const results = await proxy.multipleOutValuesAsync();
        expect(results).toEqual(['Hello', 'World', '!']);
    });

    it('does not coalesce one array into the array of return values', function () {
        proxy.oneArrayOutRemote(([result], excp) => {
            expect(result).toEqual(['Hello', 'World', '!']);
            expect(excp).toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('does not coalesce one array into the array of return values with async/await', async function () {
        const [result] = await proxy.oneArrayOutAsync();
        expect(result).toEqual(['Hello', 'World', '!']);
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

    it('does not coalesce an array of arrays into the array of return values with async/await', async function () {
        const [[a1, a2]] = await proxy.arrayOfArrayOutAsync();
        expect(a1).toEqual(['Hello', 'World']);
        expect(a2).toEqual(['World', 'Hello']);
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

    it('can return multiple arrays from an async/await method', async function () {
        const [a1, a2] = await proxy.multipleArrayOutAsync();
        expect(a1).toEqual(['Hello', 'World']);
        expect(a2).toEqual(['World', 'Hello']);
    });

    it('handles a bad signature by throwing an exception', function () {
        proxy.arrayOutBadSigRemote(function (result, excp) {
            expect(excp).not.toBeNull();
            loop.quit();
        });
        loop.run();
    });

    it('handles a bad signature in async/await by rejecting the promise', async function () {
        await expectAsync(proxy.arrayOutBadSigAsync()).toBeRejected();
    });

    it('can call a remote method that is implemented asynchronously', function () {
        let someString = 'Hello world!';
        let someInt = 42;

        proxy.echoRemote(someString, someInt,
            function (result, excp) {
                expect(excp).toBeNull();
                expect(result).toEqual([someString, someInt]);
                loop.quit();
            });
        loop.run();
    });

    it('can call an async/await method that is implemented asynchronously', async function () {
        const someString = 'Hello world!';
        const someInt = 42;

        const results = await proxy.echoAsync(someString, someInt);
        expect(results).toEqual([someString, someInt]);
    });

    it('can send and receive bytes from a remote method', function () {
        let someBytes = [0, 63, 234];
        someBytes.forEach(b => {
            proxy.byteEchoRemote(b, ([result], excp) => {
                expect(excp).toBeNull();
                expect(result).toEqual(b);
                loop.quit();
            });

            loop.run();
        });
    });

    it('can send and receive bytes from an async/await method', async function () {
        let someBytes = [0, 63, 234];
        await Promise.allSettled(someBytes.map(async b => {
            const [byte] = await proxy.byteEchoAsync(b);
            expect(byte).toEqual(b);
        }));
    });

    it('can call a remote method that returns an array of structs', function () {
        proxy.structArrayRemote(([result], excp) => {
            expect(excp).toBeNull();
            expect(result).toEqual([[128, 123456], [42, 654321]]);
            loop.quit();
        });
        loop.run();
    });

    it('can call an async/await method that returns an array of structs', async function () {
        const [result] = await proxy.structArrayAsync();
        expect(result).toEqual([[128, 123456], [42, 654321]]);
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
            expect(result['anInteger'].deepUnpack()).toEqual(10);

            // and not dropped off a double
            expect(result['aDoubleBeforeAndAfter'].deepUnpack()).toEqual(10.5);

            // check without type conversion
            expect(result['aDouble'].deepUnpack()).toBe(10.0);

            loop.quit();
        });
        loop.run();
    });

    it('can send and receive dicts from an async/await method', async function () {
        // See notes in test above
        const [result] = await proxy.dictEchoAsync({
            aDouble: new GLib.Variant('d', 10),
            anInteger: new GLib.Variant('i', 10.5),
            aDoubleBeforeAndAfter: new GLib.Variant('d', 10.5),
        });
        expect(result).not.toBeNull();
        expect(result['anInteger'].deepUnpack()).toEqual(10);
        expect(result['aDoubleBeforeAndAfter'].deepUnpack()).toEqual(10.5);
        expect(result['aDouble'].deepUnpack()).toBe(10.0);
    });

    it('can call a remote method with a Unix FD', function (done) {
        const fd = GjsTestTools.open_bytes(expectedBytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        proxy.fdInRemote(0, fdList, ([bytes], exc, outFdList) => {
            expect(exc).toBeNull();
            expect(outFdList).toBeNull();
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('can call an async/await method with a Unix FD', async function () {
        const fd = GjsTestTools.open_bytes(expectedBytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        const [bytes, outFdList] = await proxy.fdInAsync(0, fdList);
        expect(outFdList).not.toBeDefined();
        expect(bytes).toEqual(expectedBytes);
    });

    it('can call an asynchronously implemented remote method with a Unix FD', function (done) {
        const fd = GjsTestTools.open_bytes(expectedBytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        proxy.fdIn2Remote(0, fdList, ([bytes], exc, outFdList) => {
            expect(exc).toBeNull();
            expect(outFdList).toBeNull();
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('can call an asynchronously implemented async/await method with a Unix FD', async function () {
        const fd = GjsTestTools.open_bytes(expectedBytes);
        const fdList = Gio.UnixFDList.new_from_array([fd]);
        const [bytes, outFdList] = await proxy.fdIn2Async(0, fdList);
        expect(outFdList).not.toBeDefined();
        expect(bytes).toEqual(expectedBytes);
    });

    function readBytesFromFdSync(fd) {
        const stream = new GioUnix.InputStream({fd, closeFd: true});
        const bytes = stream.read_bytes(4096, null);
        return bytes.toArray();
    }

    it('can call a remote method that returns a Unix FD', function (done) {
        proxy.fdOutRemote(expectedBytes, ([fdIndex], exc, outFdList) => {
            expect(exc).toBeNull();
            const bytes = readBytesFromFdSync(outFdList.get(fdIndex));
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('can call an async/await method that returns a Unix FD', async function () {
        const [fdIndex, outFdList] = await proxy.fdOutAsync(expectedBytes);
        const bytes = readBytesFromFdSync(outFdList.get(fdIndex));
        expect(bytes).toEqual(expectedBytes);
    });

    it('can call an asynchronously implemented remote method that returns a Unix FD', function (done) {
        proxy.fdOut2Remote(expectedBytes, ([fdIndex], exc, outFdList) => {
            expect(exc).toBeNull();
            const bytes = readBytesFromFdSync(outFdList.get(fdIndex));
            expect(bytes).toEqual(expectedBytes);
            done();
        });
    });

    it('can call an asynchronously implemented asyc/await method that returns a Unix FD', async function () {
        const [fdIndex, outFdList] = await proxy.fdOut2Async(expectedBytes);
        const bytes = readBytesFromFdSync(outFdList.get(fdIndex));
        expect(bytes).toEqual(expectedBytes);
    });

    it('throws an exception when not passing a Gio.UnixFDList to a method that requires one', function () {
        expect(() => proxy.fdInRemote(0, () => {})).toThrow();
    });

    it('rejects the promise when not passing a Gio.UnixFDList to an async method that requires one', async function () {
        await expectAsync(proxy.fdInAsync(0)).toBeRejected();
    });

    it('throws an exception when passing a handle out of range of a Gio.UnixFDList', function () {
        const fdList = new Gio.UnixFDList();
        expect(() => proxy.fdInRemote(0, fdList, () => {})).toThrow();
    });

    it('rejects the promise when async passing a handle out of range of a Gio.UnixFDList', async function () {
        const fdList = new Gio.UnixFDList();
        await expectAsync(proxy.fdInAsync(0, fdList)).toBeRejected();
    });

    it('Has defined properties', function () {
        expect(Object.hasOwn(proxy, 'PropReadWrite')).toBeTruthy();
        expect(Object.hasOwn(proxy, 'PropReadOnly')).toBeTruthy();
        expect(Object.hasOwn(proxy, 'PropWriteOnly')).toBeTruthy();
        expect(Object.hasOwn(proxy, 'PropPrePacked')).toBeTruthy();
    });

    it('reading readonly property works', function () {
        expect(proxy.PropReadOnly).toEqual(PROP_READ_ONLY_INITIAL_VALUE);
    });

    it('reading readwrite property works', function () {
        expect(proxy.PropReadWrite).toEqual(
            GLib.Variant.new_string(PROP_READ_WRITE_INITIAL_VALUE.toString()));
    });

    it('reading writeonly throws an error', function () {
        expect(() => proxy.PropWriteOnly).toThrowError('Property PropWriteOnly is not readable');
    });

    it('Setting a readwrite property works', function () {
        let testStr = 'GjsVariantValue';
        expect(() => {
            proxy.PropReadWrite = GLib.Variant.new_string(testStr);
        }).not.toThrow();

        expect(proxy.PropReadWrite.deepUnpack()).toEqual(testStr);

        expect(waitForServerProperty('_propReadWrite', testStr)).toEqual(testStr);
    });

    it('Setting a writeonly property works', function () {
        let testValue = Math.random().toString();
        expect(() => {
            proxy.PropWriteOnly = testValue;
        }).not.toThrow();

        expect(() => proxy.PropWriteOnly).toThrow();
        expect(waitForServerProperty('_propWriteOnly', testValue)).toEqual(testValue);
    });

    it('Setting a readonly property throws an error', function () {
        let testValue = Math.random().toString();
        expect(() => {
            proxy.PropReadOnly = testValue;
        }).toThrowError('Property PropReadOnly is not writable');

        expect(proxy.PropReadOnly).toBe(PROP_READ_ONLY_INITIAL_VALUE);
    });

    it('Reading a property that prepacks the return value works', function () {
        expect(proxy.PropPrePacked.member).toBeInstanceOf(GLib.Variant);
        expect(proxy.PropPrePacked.member.get_type_string()).toEqual('s');
    });

    it('Marking a property as invalidated works', function () {
        let changedProps = {};
        let invalidatedProps = [];

        proxy.connect('g-properties-changed', (proxy_, changed, invalidated) => {
            changedProps = changed.deepUnpack();
            invalidatedProps = invalidated;
            loop.quit();
        });

        test.emitPropertyChanged('PropReadOnly', null);
        loop.run();

        expect(changedProps).not.toContain('PropReadOnly');
        expect(invalidatedProps).toContain('PropReadOnly');
    });
});


describe('DBus Proxy wrapper', function () {
    let loop;
    let wasPromise;
    let writerFunc;

    beforeAll(function () {
        loop = new GLib.MainLoop(null, false);

        wasPromise = Gio.DBusProxy.prototype._original_init_async instanceof Function;
        Gio._promisify(Gio.DBusProxy.prototype, 'init_async');

        writerFunc = jasmine.createSpy(
            'log writer func', (level, fields) => {
                const decoder = new TextDecoder('utf-8');
                const domain = decoder.decode(fields?.GLIB_DOMAIN);
                const message = `${decoder.decode(fields?.MESSAGE)}\n`;
                level |= GLib.LogLevelFlags.FLAG_RECURSION;
                GLib.log_default_handler(domain, level, message, null);
                return GLib.LogWriterOutput.HANDLED;
            });

        writerFunc.and.callThrough();
        GLib.log_set_writer_func(writerFunc);
    });

    beforeEach(function () {
        writerFunc.calls.reset();
    });

    afterAll(function () {
        GLib.log_set_writer_default();
    });

    it('init failures are reported in sync mode', function () {
        const cancellable = new Gio.Cancellable();
        cancellable.cancel();
        expect(() => new ProxyClass(Gio.DBus.session, 'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test',
            Gio.DBusProxyFlags.NONE,
            cancellable)).toThrow();
    });

    it('init failures are reported in async mode', function () {
        const cancellable = new Gio.Cancellable();
        cancellable.cancel();
        const initDoneSpy = jasmine.createSpy(
            'init finish func', () => loop.quit());
        initDoneSpy.and.callThrough();
        new ProxyClass(Gio.DBus.session, 'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test',
            initDoneSpy, cancellable, Gio.DBusProxyFlags.NONE);
        loop.run();

        expect(initDoneSpy).toHaveBeenCalledTimes(1);
        const {args: callArgs} = initDoneSpy.calls.mostRecent();
        expect(callArgs.at(0)).toBeNull();
        expect(callArgs.at(1).matches(
            Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED)).toBeTrue();
    });

    it('can init a proxy asynchronously when promisified', function () {
        new ProxyClass(Gio.DBus.session, 'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test',
            () => loop.quit(),
            Gio.DBusProxyFlags.NONE);
        loop.run();

        expect(writerFunc).not.toHaveBeenCalled();
    });

    it('can create a proxy from a promise', async function () {
        const proxyPromise = ProxyClass.newAsync(Gio.DBus.session, 'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test');
        await expectAsync(proxyPromise).toBeResolved();
    });

    it('can create fail a proxy from a promise', async function () {
        const cancellable = new Gio.Cancellable();
        cancellable.cancel();
        const proxyPromise = ProxyClass.newAsync(Gio.DBus.session, 'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test', cancellable);
        await expectAsync(proxyPromise).toBeRejected();
    });

    afterAll(function () {
        if (!wasPromise) {
            // Remove stuff added by Gio._promisify, this can be not needed in future
            // nor should break other tests, but we didn't want to depend on those.
            expect(Gio.DBusProxy.prototype._original_init_async).toBeInstanceOf(Function);
            Gio.DBusProxy.prototype.init_async = Gio.DBusProxy.prototype._original_init_async;
            delete Gio.DBusProxy.prototype._original_init_async;
        }
    });
});
