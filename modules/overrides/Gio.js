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
var GObject = imports.gi.GObject;
var GjsPrivate = imports.gi.GjsPrivate;
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

    var inVariant = new GLib.Variant('(' + inSignature.join('') + ')', arg_array);

    var asyncCallback = function (proxy, result) {
        var outVariant = null, succeeded = false;
        try {
            outVariant = proxy.call_finish(result);
            succeeded = true;
        } catch (e) {
            replyFunc(null, e);
        }

        if (succeeded)
            replyFunc(outVariant.deep_unpack(), null);
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
    if (exc != null) {
        log("Ignored exception from dbus method: " + exc.toString());
    }
}

function _makeProxyMethod(method, sync) {
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
    let variant = new GLib.Variant(signature, value);
    this.set_cached_property(name, variant);

    this.call('org.freedesktop.DBus.Properties.Set',
              new GLib.Variant('(ssv)',
                               [this.g_interface_name,
                                name, variant]),
              Gio.DBusCallFlags.NONE, -1, null,
              Lang.bind(this, function(proxy, result) {
                  try {
                      this.call_finish(result);
                  } catch(e) {
                      log('Could not set property ' + name + ' on remote object ' +
                          this.g_object_path + ': ' + e.message);
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
        Object.defineProperty(this, name, { get: Lang.bind(this, _propertyGetter, name),
                                            set: Lang.bind(this, _propertySetter, name, signature),
                                            configurable: true,
                                            enumerable: true });
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
                let caughtErrorWhenInitting = null;
                try {
                    initable.init_finish(result);
                } catch(e) {
                    caughtErrorWhenInitting = e;
                }

                if (caughtErrorWhenInitting === null) {
                    asyncCallback(initable, null);
                } else {
                    asyncCallback(null, caughtErrorWhenInitting);
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
    var nodeInfo = Gio.DBusNodeInfo.new_for_xml(value);
    return nodeInfo.interfaces[0];
}

function _injectToMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function() {
        addition.apply(this, arguments);
        return previous.apply(this, arguments);
    };
}

function _wrapFunction(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function() {
        var args = Array.prototype.slice.call(arguments);
        args.unshift(previous);
        return addition.apply(this, args);
    };
}

function _makeOutSignature(args) {
    var ret = '(';
    for (var i = 0; i < args.length; i++)
        ret += args[i].signature;

    return ret + ')';
}

function _handleMethodCall(info, impl, method_name, parameters, invocation) {
    // prefer a sync version if available
    if (this[method_name]) {
        let retval;
        try {
            retval = this[method_name].apply(this, parameters.deep_unpack());
        } catch (e) {
            if (e instanceof GLib.Error) {
                invocation.return_gerror(e);
            } else {
                let name = e.name;
                if (name.indexOf('.') == -1) {
                    // likely to be a normal JS error
                    name = 'org.gnome.gjs.JSError.' + name;
                }
                logError(e, "Exception in method call: " + method_name);
                invocation.return_dbus_error(name, e.message);
            }
            return;
        }
        if (retval === undefined) {
            // undefined (no return value) is the empty tuple
            retval = new GLib.Variant('()', []);
        }
        try {
            if (!(retval instanceof GLib.Variant)) {
                // attempt packing according to out signature
                let methodInfo = info.lookup_method(method_name);
                let outArgs = methodInfo.out_args;
                let outSignature = _makeOutSignature(outArgs);
                if (outArgs.length == 1) {
                    // if one arg, we don't require the handler wrapping it
                    // into an Array
                    retval = [retval];
                }
                retval = new GLib.Variant(outSignature, retval);
            }
            invocation.return_value(retval);
        } catch(e) {
            // if we don't do this, the other side will never see a reply
            invocation.return_dbus_error('org.gnome.gjs.JSError.ValueError',
                                         "Service implementation returned an incorrect value type");
        }
    } else if (this[method_name + 'Async']) {
        this[method_name + 'Async'](parameters.deep_unpack(), invocation);
    } else {
        log('Missing handler for DBus method ' + method_name);
        invocation.return_gerror(new Gio.DBusError({ code: Gio.DBusError.UNKNOWN_METHOD,
                                                     message: 'Method ' + method_name + ' is not implemented' }));
    }
}

function _handlePropertyGet(info, impl, property_name) {
    let propInfo = info.lookup_property(property_name);
    let jsval = this[property_name];
    if (jsval != undefined)
        return new GLib.Variant(propInfo.signature, jsval);
    else
        return null;
}

function _handlePropertySet(info, impl, property_name, new_value) {
    this[property_name] = new_value.deep_unpack();
}

function _wrapJSObject(interfaceInfo, jsObj) {
    var info;
    if (interfaceInfo instanceof Gio.DBusInterfaceInfo)
        info = interfaceInfo;
    else
        info = Gio.DBusInterfaceInfo.new_for_xml(interfaceInfo);
    info.cache_build();

    var impl = new GjsPrivate.DBusImplementation({ g_interface_info: info });
    impl.connect('handle-method-call', function(impl, method_name, parameters, invocation) {
        return _handleMethodCall.call(jsObj, info, impl, method_name, parameters, invocation);
    });
    impl.connect('handle-property-get', function(impl, property_name) {
        return _handlePropertyGet.call(jsObj, info, impl, property_name);
    });
    impl.connect('handle-property-set', function(impl, property_name, value) {
        return _handlePropertySet.call(jsObj, info, impl, property_name, value);
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
        unwatch_name:             Gio.bus_unwatch_name
    };

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

    _injectToMethod(Gio.DBusProxy.prototype, 'init', _addDBusConvenience);
    _injectToMethod(Gio.DBusProxy.prototype, 'init_async', _addDBusConvenience);
    Gio.DBusProxy.prototype.connectSignal = Signals._connect;
    Gio.DBusProxy.prototype.disconnectSignal = Signals._disconnect;

    Gio.DBusProxy.makeProxyWrapper = _makeProxyWrapper;

    // Some helpers
    _wrapFunction(Gio.DBusNodeInfo, 'new_for_xml', _newNodeInfo);
    Gio.DBusInterfaceInfo.new_for_xml = _newInterfaceInfo;

    Gio.DBusExportedObject = GjsPrivate.DBusImplementation;
    Gio.DBusExportedObject.wrapJSObject = _wrapJSObject;
}
