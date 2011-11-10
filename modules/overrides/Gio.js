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
var GjsDBus = imports.gi.GjsDBus;
var Lang = imports.lang;
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
	return this.call(methodName,
			 inVariant,
			 flags,
			 -1,
			 cancellable,
			 asyncCallback);
    }
}

function _logReply(result, exc) {
    if (result != null) {
        log("Ignored reply to dbus method: " + result);
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
    Signals._emit.call(proxy, signal_name, sender_name, parameters.deep_unpack());
}

function _propertyGetter(name) {
    let value = this.get_cached_property(name);
    return value ? value.deep_unpack() : null;
}

function _propertySetter(value, name, signature) {
    let variant = GLib.Variant.new(signature, value);
    this.set_cached_property(name, variant);

    this.call('org.freedesktop.DBus.Properties.Set',
	      GLib.Variant.new('(ssv)',
			       [this.g_interface_name,
				name, variant]),
	      Gio.DBusCallFlags.NONE, -1, null,
	      Lang.bind(this, function(proxy, result) {
		  try {
		      this.call_finish(result);
		  } catch(e) {
		      log('Could not set property ' + name + ' on remote object ' +
			  this.g_object_path, '. Error is ' + e.message);
		  }
	      }));
}

function _addDBusConvenience() {
    let info = this.g_interface_info;
    if (!info)
	return;

    if (info.signals.length > 0)
	this.connect('g-signal', _convertToNativeSignal);

    let i, methods = info.methods;
    for (i = 0; i < methods.length; i++) {
	var method = methods[i];
	this[method.name + 'Remote'] = _makeProxyMethod(methods[i], false);
	this[method.name + 'Sync'] = _makeProxyMethod(methods[i], true);
    }

    let properties = info.properties;
    for (i = 0; i < properties.length; i++) {
	let name = properties[i].name;
	let signature = properties[i].signature;
	Lang.defineAccessorProperty(this, name,
				    Lang.bind(this, _propertyGetter, name),
				    Lang.bind(this, _propertySetter, name, signature));
    }
}

function _makeProxyWrapper(interfaceXml) {
    var info = _newInterfaceInfo(interfaceXml);
    var iname = info.name;
    return function(bus, name, object, asyncCallback, cancellable) {
	var obj = new Gio.DBusProxy({ g_connection: bus,
				      g_interface_name: iname,
				      g_interface_info: info,
				      g_name: name,
				      g_object_path: object });
	if (!cancellable)
	    cancellable = null;
	if (asyncCallback)
	    obj.init_async(GLib.PRIORITY_DEFAULT, cancellable, function(initable, result) {
		try {
		    initable.init_finish(result);
		    asyncCallback(initable, null);
		} catch(e) {
		    asyncCallback(null, e);
		}
	    });
	else
	    obj.init(cancellable);
	return obj;
    };
}


function _newNodeInfo(constructor, value) {
    if (typeof value == 'string')
	return constructor(value);
    else if (value instanceof XML)
	return constructor(value.toXMLString());
    else
	throw TypeError('Invalid type ' + Object.prototype.toString.call(value));
}

function _newInterfaceInfo(value) {
    var xml;
    if (typeof value == 'string')
	xml = new XML(value);
    else if (value instanceof XML)
	xml = value;
    else
	throw TypeError('Invalid type ' + Object.prototype.toString.call(value));

    var node;
    if (value.name() == 'interface') {
	// wrap inside a node
	node = <node/>;
	node.node += xml;
    } else
	node = xml;

    var nodeInfo = Gio.DBusNodeInfo.new_for_xml(node);
    return nodeInfo.interfaces[0];
}

function _injectToMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function() {
	addition.apply(this, arguments);
	return previous.apply(this, arguments);
    }
}

function _wrapFunction(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function() {
	var args = Array.prototype.slice.call(arguments);
	args.unshift(previous);
	return addition.apply(this, args);
    }
}

function _makeOutSignature(args) {
    var ret = '(';
    for (var i = 0; i < args.length; i++)
	ret += args[i].signature;

    return ret + ')';
}

function _wrapJSObject(interfaceInfo, jsObj) {
    var info;
    if (interfaceInfo instanceof Gio.DBusInterfaceInfo)
	info = interfaceInfo
    else
	info = Gio.DBusInterfaceInfo.new_for_xml(interfaceInfo);
    info.cache_build();

    var impl = new GjsDBus.Implementation({ g_interface_info: info });
    impl.connect('handle-method-call', function(impl, method_name, parameters, invocation) {
	// prefer a sync version if available
	if (jsObj[method_name]) {
	    var retval;
	    try {
		retval = jsObj[method_name].apply(jsObj, parameters.deep_unpack());
	    } catch (e) {
		if (e.name.indexOf('.') == -1) {
		    // likely to be a normal JS error
		    e.name = 'org.gnome.gjs.JSError.' + e.name;
		}
		invocation.return_dbus_error(e.name, e.message);
		return;
	    }
	    if (retval === undefined) {
		// undefined (no return value) is the empty tuple
		retval = GLib.Variant.new_tuple([], 0);
	    }
	    try {
		if (!(retval instanceof GLib.Variant)) {
		    // attemp packing according to out signature
		    var methodInfo = info.lookup_method(method_name);
		    var outArgs = methodInfo.out_args;
		    var outSignature = _makeOutSignature(outArgs);
		    if (outArgs.length == 1) {
			// if one arg, we don't require the handler wrapping it
			// into an Array
			retval = [retval];
		    }
		    retval = GLib.Variant.new(outSignature, retval);
		}
		invocation.return_value(retval);
	    } catch(e) {
		// if we don't do this, the other side will never see a reply
		invocation.return_dbus_error('org.gnome.gjs.JSError.ValueError',
					     "The return value from the method handler was not in the correct format");
	    }
	} else if (jsObj[method_name + 'Async']) {
	    jsObj[method_name + 'Async'](parameters.deep_unpack(), invocation);
	} else {
	    log('Missing handler for DBus method ' + method_name);
	    invocation.return_dbus_error('org.gnome.gjs.NotImplementedError',
					 "Method ' + method_name + ' is not implemented");
	}
    });
    impl.connect('handle-property-get', function(impl, property_name) {
	var propInfo = info.lookup_property(property_name);
	var jsval = jsObj[property_name];
	if (jsval != undefined)
	    return GLib.Variant.new(propInfo.signature, jsval);
	else
	    return null;
    });
    impl.connect('handle-property-set', function(impl, property_name, new_value) {
	jsObj[property_name] = new_value.deep_unpack();
    });

    return impl;
}

function _init() {
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
	return Gio.bus_unown_name(id);
    };

    // This should be done inside a constructor, but it cannot currently
    _injectToMethod(Gio.DBusProxy.prototype, 'init', _addDBusConvenience);
    _injectToMethod(Gio.DBusProxy.prototype, 'init_async', _addDBusConvenience);
    Gio.DBusProxy.prototype.connectSignal = Signals._connect;
    Gio.DBusProxy.prototype.disconnectSignal = Signals._disconnect;

    Gio.DBusProxy.makeProxyWrapper = _makeProxyWrapper;

    // Some helpers
    _wrapFunction(Gio.DBusNodeInfo, 'new_for_xml', _newNodeInfo);
    Gio.DBusInterfaceInfo.new_for_xml = _newInterfaceInfo;

    // More or less...
    Gio.DBusExportedObject = GjsDBus.Implementation;
    Gio.DBusExportedObject.wrapJSObject = _wrapJSObject;
}
