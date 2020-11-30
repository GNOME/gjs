// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Andy Holmes <andrew.g.r.holmes@gmail.com>

'use strict';

const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;


/*
 * An XML DBus Interface
 */
const ifaceXml = `
<node>
  <interface name="org.gnome.gjs.Test">
    <method name="SimpleMethod"/>
    <method name="ComplexMethod">
      <arg type="s" direction="in" name="input"/>
      <arg type="u" direction="out" name="length"/>
    </method>
    <signal name="TestSignal">
      <arg name="type" type="s"/>
      <arg name="value" type="b"/>
    </signal>
    <property name="ReadOnlyProperty" type="s" access="read"/>
    <property name="ReadWriteProperty" type="b" access="readwrite"/>
  </interface>
</node>`;



// Pass the XML string to make a re-usable proxy class for an interface proxies.
const TestProxy = Gio.DBusProxy.makeProxyWrapper(ifaceXml);


let proxy = null;
let proxySignalId = 0;
let proxyPropId = 0;


// Watching a name on DBus. Another option is to create a proxy with the
// `Gio.DBusProxyFlags.DO_NOT_AUTO_START` flag and watch the `g-name-owner`
// property.
function onNameAppeared(connection, name, _owner) {
    print(`"${name}" appeared on the session bus`);

    // If creating a proxy synchronously, errors will be thrown as normal
    try {
        proxy = new TestProxy(
            Gio.DBus.session,
            'org.gnome.gjs.Test',
            '/org/gnome/gjs/Test'
        );
    } catch (e) {
        logError(e);
        return;
    }


    // Proxy wrapper signals use the special functions `connectSignal()` and
    // `disconnectSignal()` to avoid conflicting with regular GObject signals.
    proxySignalId = proxy.connectSignal('TestSignal', (proxy_, name_, args) => {
        print(`TestSignal: ${args[0]}, ${args[1]}`);
    });


    // To watch property changes, you can connect to the `g-properties-changed`
    // GObject signal with `connect()`
    proxyPropId = proxy.connect('g-properties-changed', (proxy_, changed, invalidated) => {
        for (let [prop, value] of Object.entries(changed.deepUnpack()))
            print(`Property '${prop}' changed to '${value.deepUnpack()}'`);

        for (let prop of invalidated)
            print(`Property '${prop}' invalidated`);
    });


    // Reading and writing properties is straight-forward
    print(`ReadOnlyProperty: ${proxy.ReadOnlyProperty}`);

    print(`ReadWriteProperty: ${proxy.ReadWriteProperty}`);

    proxy.ReadWriteProperty = !proxy.ReadWriteProperty;
    print(`ReadWriteProperty: ${proxy.ReadWriteProperty}`);


    // Both synchronous and asynchronous functions will be generated
    try {
        let value = proxy.SimpleMethodSync();

        print(`SimpleMethod: ${value}`);
    } catch (e) {
        logError(`SimpleMethod: ${e.message}`);
    }

    proxy.ComplexMethodRemote('input string', (value, error, fdList) => {
        // If @error is not `null`, then an error occurred
        if (error !== null) {
            logError(error);
            return;
        }

        print(`ComplexMethod: ${value}`);

        // Methods that return file descriptors are fairly rare, so you should
        // know to expect one or not.
        if (fdList !== null) {
            //
        }
    });
}

function onNameVanished(connection, name) {
    print(`"${name}" vanished from the session bus`);

    if (proxy !== null) {
        proxy.disconnectSignal(proxySignalId);
        proxy.disconnect(proxyPropId);
        proxy = null;
    }
}

let busWatchId = Gio.bus_watch_name(
    Gio.BusType.SESSION,
    'org.gnome.gjs.Test',
    Gio.BusNameWatcherFlags.NONE,
    onNameAppeared,
    onNameVanished
);

// Start an event loop
let loop = GLib.MainLoop.new(null, false);
loop.run();

// Unwatching names works just like disconnecting signal handlers.
Gio.bus_unown_name(busWatchId);


/* Asynchronous Usage
 *
 * Below is the alternative, asynchronous usage of proxy wrappers. If creating
 * a proxy asynchronously, you should not consider the proxy ready to use until
 * the callback is invoked without error.
 */
proxy = null;

new TestProxy(
    Gio.DBus.session,
    'org.gnome.gjs.Test',
    '/org/gnome/gjs/Test',
    (sourceObj, error) => {
        // If @error is not `null` it will be an Error object indicating the
        // failure. @proxy will be `null` in this case.
        if (error !== null) {
            logError(error);
            return;
        }

        // At this point the proxy is initialized and you can start calling
        // functions, using properties and so on.
        proxy = sourceObj;
        print(`ReadOnlyProperty: ${proxy.ReadOnlyProperty}`);
    },
    // Optional Gio.Cancellable object. Pass `null` if you need to pass flags.
    null,
    // Optional flags passed to the Gio.DBusProxy constructor
    Gio.DBusProxyFlags.NONE
);

