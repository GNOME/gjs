// application/javascript;version=1.8
// Copyright 2011 Giovanni Campagna
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

var GLib = imports.gi.GLib;
var Signals = imports.signals;
var Gio;

function _signatureLength(sig) {
    var counter = 0;
    // make it an array
    var signature = Array.prototype.slice.call(sig);
    while (signature.length) {
	GLib._read_single_type(sig);
	counter++;
    }
    return counter;
}

function _proxyInvoker(methodName, sync, inSignature, arg_array) {
    var replyFunc;
    var flags = 0;
    var cancellable = null;

    /* Convert arg_array to a *real* array */
    arg_array = Array.prototype.slice.call(arg_array);

    /* The default replyFunc only logs the responses */
    replyFunc = _logReply;

    var signatureLength = inSignature.length;
    var minNumberArgs = signatureLength;
    var maxNumberArgs = signatureLength + 3;

    if (arg_array.length < minNumberArgs) {
        throw new Error("Not enough arguments passed for method: " + methodName +
                       ". Expected " + minNumberArgs + ", got " + arg_array.length);
    } else if (arg_array.length > maxNumberArgs) {
        throw new Error("Too many arguments passed for method: " + methodName +
                       ". Maximum is " + maxNumberArgs +
                        " + one callback and/or flags");
    }

    while (arg_array.length > signatureLength) {
        var argNum = arg_array.length - 1;
        var arg = arg_array.pop();
        if (typeof(arg) == "function" && !sync) {
            replyFunc = arg;
        } else if (typeof(arg) == "number") {
            flags = arg;
	} else if (arg instanceof Gio.Cancellable) {
	    cancellable = arg;
        } else {
            throw new Error("Argument " + argNum + " of method " + methodName +
                            " is " + typeof(arg) + ". It should be a callback, flags or a Gio.Cancellable");
        }
    }

    var inVariant = GLib.Variant.new('(' + inSignature.join('') + ')', arg_array);

    var asyncCallback = function (proxy, result) {
	try {
	    var outVariant = proxy.call_finish(result);
	    // XXX: is deep_unpack appropriate here?
	    // (it converts everything to a JS type, except for nested Variants)
	    replyFunc(outVariant.deep_unpack(), null);
	} catch (e) {
	    replyFunc(null, e);
	}
    };

    if (sync) {
	return this.call_sync(methodName,
			      inVariant,
			      flags,
			      -1,
			      cancellable).deep_unpack();
    } else {
	this.call_async(methodName,
			inVariant,
			flags,
			-1,
			cancellable,
			asyncCallback);
    }
}

function _logReply(result, exc) {
    if (result != null) {
        log("Ignored reply to dbus method: " + result.toSource());
    }
    if (exc != null) {
        log("Ignored exception from dbus method: " + exc.toString());
    }
}

function _makeProxyMethod(method, sync) {
    /* JSON methods are the default */
    var i;
    var name = method.name;
    var inArgs = method.in_args;
    var inSignature = [ ];
    for (i = 0; i < inArgs.length; i++)
	inSignature.push(inArgs[i].signature);

    return function() {
        return _proxyInvoker.call(this, name, sync, inSignature, arguments);
    };
}

function _convertToNativeSignal(proxy, sender_name, signal_name, parameters) {
    Signals._emit.call(this, signal_name, sender_name, GLib.Variant.deep_unpack(paramters));
}

function _addDBusConvenience() {
    var info = this.g_interface_info;
    if (!info)
	return;

    if (info.signals.length > 0)
	this.connect('g-signal', Lang.bind(this, _convertToNativeSignal));

    var i, methods = info.methods;
    for (i = 0; i < methods.length; i++) {
	var method = methods[i];
	this[method.name + 'Remote'] = _makeProxyMethod(methods[i], false);
	this[method.name + 'Sync'] = _makeProxyMethod(methods[i], true);
    }
}

function _injectToMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function() {
	addition.apply(this, arguments);
	return previous.apply(this, arguments);
    }
}

 function __init__() {
    Gio = this;

    Gio.DBus = {
	get session() {
	    return Gio.bus_get_sync(Gio.BusType.SESSION, null);
	},
	get system() {
	    return Gio.bus_get_sync(Gio.BusType.SYSTEM, null);
	},

	// Namespace some functions
	get:        Gio.bus_get,
	get_finish: Gio.bus_get_finish,
	get_sync:   Gio.bus_get_sync,

	own_name:               Gio.bus_own_name,
	own_name_on_connection: Gio.bus_own_name_on_connection,
	unown_name:             Gio.bus_unown_name,

	watch_name:               Gio.bus_watch_name,
	watch_name_on_connection: Gio.bus_watch_name_on_connection,
	unwatch_name:             Gio.bus_unwatch_name,
    };

    // Some helpers to support current style (like DBus.system.watch_name)
    Gio.DBusConnection.prototype.watch_name = function(name, flags, appeared, vanished) {
	return Gio.bus_watch_name_on_connection(this, name, flags, appeared, vanished);
    };
    Gio.DBusConnection.prototype.unwatch_name = function(id) {
	return Gio.bus_unwatch_name(id);
    };
    Gio.DBusConnection.prototype.own_name = function(name, flags, acquired, lost) {
	return Gio.bus_own_name_on_connection(this, name, flags, acquired, lost);
    };
    Gio.DBusConnection.prototype.unown_name = function(id) {
	return Gio.unown_name(id);
    };

    // This should be done inside a constructor, but it cannot currently
    
    _injectToMethod(Gio.DBusProxy.prototype, 'init', _addDBusConvenience);
    _injectToMethod(Gio.DBusProxy.prototype, 'async_init', _addDBusConvenience);
    Gio.DBusProxy.prototype.connectSignal = Signals._connect;
    Gio.DBusProxy.prototype.disconnectSignal = Signals._disconnect;
 }
