// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Giovanni Campagna

var GLib = imports.gi.GLib;
var GjsPrivate = imports.gi.GjsPrivate;
var Signals = imports.signals;
var Gio;

// Ensures that a Gio.UnixFDList being passed into or out of a DBus method with
// a parameter type that includes 'h' somewhere, actually has entries in it for
// each of the indices being passed as an 'h' parameter.
function _validateFDVariant(variant, fdList) {
    switch (String.fromCharCode(variant.classify())) {
    case 'b':
    case 'y':
    case 'n':
    case 'q':
    case 'i':
    case 'u':
    case 'x':
    case 't':
    case 'd':
    case 'o':
    case 'g':
    case 's':
        return;
    case 'h': {
        const val = variant.get_handle();
        const numFds = fdList.get_length();
        if (val >= numFds) {
            throw new Error(`handle ${val} is out of range of Gio.UnixFDList ` +
                `containing ${numFds} FDs`);
        }
        return;
    }
    case 'v':
        _validateFDVariant(variant.get_variant(), fdList);
        return;
    case 'm': {
        let val = variant.get_maybe();
        if (val)
            _validateFDVariant(val, fdList);
        return;
    }
    case 'a':
    case '(':
    case '{': {
        let nElements = variant.n_children();
        for (let ix = 0; ix < nElements; ix++)
            _validateFDVariant(variant.get_child_value(ix), fdList);
        return;
    }
    }

    throw new Error('Assertion failure: this code should not be reached');
}

function _proxyInvoker(methodName, sync, inSignature, argArray) {
    var replyFunc;
    var flags = 0;
    var cancellable = null;
    let fdList = null;

    /* Convert argArray to a *real* array */
    argArray = Array.prototype.slice.call(argArray);

    /* The default replyFunc only logs the responses */
    replyFunc = _logReply;

    var signatureLength = inSignature.length;
    var minNumberArgs = signatureLength;
    var maxNumberArgs = signatureLength + 4;

    if (argArray.length < minNumberArgs) {
        throw new Error(`Not enough arguments passed for method: ${
            methodName}. Expected ${minNumberArgs}, got ${argArray.length}`);
    } else if (argArray.length > maxNumberArgs) {
        throw new Error(`Too many arguments passed for method ${methodName}. ` +
            `Maximum is ${maxNumberArgs} including one callback, ` +
            'Gio.Cancellable, Gio.UnixFDList, and/or flags');
    }

    while (argArray.length > signatureLength) {
        var argNum = argArray.length - 1;
        var arg = argArray.pop();
        if (typeof arg === 'function' && !sync) {
            replyFunc = arg;
        } else if (typeof arg === 'number') {
            flags = arg;
        } else if (arg instanceof Gio.Cancellable) {
            cancellable = arg;
        } else if (arg instanceof Gio.UnixFDList) {
            fdList = arg;
        } else {
            throw new Error(`Argument ${argNum} of method ${methodName} is ` +
                `${typeof arg}. It should be a callback, flags, ` +
                'Gio.UnixFDList, or a Gio.Cancellable');
        }
    }

    const inTypeString = `(${inSignature.join('')})`;
    const inVariant = new GLib.Variant(inTypeString, argArray);
    if (inTypeString.includes('h')) {
        if (!fdList) {
            throw new Error(`Method ${methodName} with input type containing ` +
                '\'h\' must have a Gio.UnixFDList as an argument');
        }
        _validateFDVariant(inVariant, fdList);
    }

    var asyncCallback = (proxy, result) => {
        try {
            const [outVariant, outFdList] =
                proxy.call_with_unix_fd_list_finish(result);
            replyFunc(outVariant.deepUnpack(), null, outFdList);
        } catch (e) {
            replyFunc([], e, null);
        }
    };

    if (sync) {
        const [outVariant, outFdList] = this.call_with_unix_fd_list_sync(
            methodName, inVariant, flags, -1, fdList, cancellable);
        if (fdList)
            return [outVariant.deepUnpack(), outFdList];
        return outVariant.deepUnpack();
    }

    return this.call_with_unix_fd_list(methodName, inVariant, flags, -1, fdList,
        cancellable, asyncCallback);
}

function _logReply(result, exc) {
    if (exc !== null)
        log(`Ignored exception from dbus method: ${exc}`);
}

function _makeProxyMethod(method, sync) {
    var i;
    var name = method.name;
    var inArgs = method.in_args;
    var inSignature = [];
    for (i = 0; i < inArgs.length; i++)
        inSignature.push(inArgs[i].signature);

    return function (...args) {
        return _proxyInvoker.call(this, name, sync, inSignature, args);
    };
}

function _convertToNativeSignal(proxy, senderName, signalName, parameters) {
    Signals._emit.call(proxy, signalName, senderName, parameters.deepUnpack());
}

function _propertyGetter(name) {
    let value = this.get_cached_property(name);
    return value ? value.deepUnpack() : null;
}

function _propertySetter(name, signature, value) {
    let variant = new GLib.Variant(signature, value);
    this.set_cached_property(name, variant);

    this.call('org.freedesktop.DBus.Properties.Set',
        new GLib.Variant('(ssv)', [this.g_interface_name, name, variant]),
        Gio.DBusCallFlags.NONE, -1, null,
        (proxy, result) => {
            try {
                this.call_finish(result);
            } catch (e) {
                log(`Could not set property ${name} on remote object ${
                    this.g_object_path}: ${e.message}`);
            }
        });
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
        this[`${method.name}Remote`] = _makeProxyMethod(methods[i], false);
        this[`${method.name}Sync`] = _makeProxyMethod(methods[i], true);
    }

    let properties = info.properties;
    for (i = 0; i < properties.length; i++) {
        let name = properties[i].name;
        let signature = properties[i].signature;
        let flags = properties[i].flags;
        let getter = () => {
            throw new Error(`Property ${name} is not readable`);
        };
        let setter = () => {
            throw new Error(`Property ${name} is not writable`);
        };

        if (flags & Gio.DBusPropertyInfoFlags.READABLE)
            getter = _propertyGetter.bind(this, name);

        if (flags & Gio.DBusPropertyInfoFlags.WRITABLE)
            setter = _propertySetter.bind(this, name, signature);

        Object.defineProperty(this, name, {
            get: getter,
            set: setter,
            configurable: false,
            enumerable: true,
        });
    }
}

function _makeProxyWrapper(interfaceXml) {
    var info = _newInterfaceInfo(interfaceXml);
    var iname = info.name;
    return function (bus, name, object, asyncCallback, cancellable,
        flags = Gio.DBusProxyFlags.NONE) {
        var obj = new Gio.DBusProxy({
            g_connection: bus,
            g_interface_name: iname,
            g_interface_info: info,
            g_name: name,
            g_flags: flags,
            g_object_path: object,
        });

        if (!cancellable)
            cancellable = null;
        if (asyncCallback) {
            obj.init_async(GLib.PRIORITY_DEFAULT, cancellable, (initable, result) => {
                let caughtErrorWhenInitting = null;
                try {
                    initable.init_finish(result);
                } catch (e) {
                    caughtErrorWhenInitting = e;
                }

                if (caughtErrorWhenInitting === null)
                    asyncCallback(initable, null);
                else
                    asyncCallback(null, caughtErrorWhenInitting);
            });
        } else {
            obj.init(cancellable);
        }
        return obj;
    };
}


function _newNodeInfo(constructor, value) {
    if (typeof value === 'string')
        return constructor(value);
    throw TypeError(`Invalid type ${Object.prototype.toString.call(value)}`);
}

function _newInterfaceInfo(value) {
    var nodeInfo = Gio.DBusNodeInfo.new_for_xml(value);
    return nodeInfo.interfaces[0];
}

function _injectToMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function (...args) {
        addition.apply(this, args);
        return previous.apply(this, args);
    };
}

function _injectToStaticMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function (...parameters) {
        let obj = previous.apply(this, parameters);
        addition.apply(obj, parameters);
        return obj;
    };
}

function _wrapFunction(klass, method, addition) {
    var previous = klass[method];

    klass[method] = function (...args) {
        args.unshift(previous);
        return addition.apply(this, args);
    };
}

function _makeOutSignature(args) {
    var ret = '(';
    for (var i = 0; i < args.length; i++)
        ret += args[i].signature;

    return `${ret})`;
}

function _handleMethodCall(info, impl, methodName, parameters, invocation) {
    // prefer a sync version if available
    if (this[methodName]) {
        let retval;
        try {
            const fdList = invocation.get_message().get_unix_fd_list();
            retval = this[methodName](...parameters.deepUnpack(), fdList);
        } catch (e) {
            if (e instanceof GLib.Error) {
                invocation.return_gerror(e);
            } else {
                let name = e.name;
                if (!name.includes('.')) {
                    // likely to be a normal JS error
                    name = `org.gnome.gjs.JSError.${name}`;
                }
                logError(e, `Exception in method call: ${methodName}`);
                invocation.return_dbus_error(name, e.message);
            }
            return;
        }
        if (retval === undefined) {
            // undefined (no return value) is the empty tuple
            retval = new GLib.Variant('()', []);
        }
        try {
            let outFdList = null;
            if (!(retval instanceof GLib.Variant)) {
                // attempt packing according to out signature
                let methodInfo = info.lookup_method(methodName);
                let outArgs = methodInfo.out_args;
                let outSignature = _makeOutSignature(outArgs);
                if (outSignature.includes('h') &&
                    retval[retval.length - 1] instanceof Gio.UnixFDList) {
                    outFdList = retval.pop();
                } else if (outArgs.length === 1) {
                    // if one arg, we don't require the handler wrapping it
                    // into an Array
                    retval = [retval];
                }
                retval = new GLib.Variant(outSignature, retval);
            }
            invocation.return_value_with_unix_fd_list(retval, outFdList);
        } catch (e) {
            // if we don't do this, the other side will never see a reply
            invocation.return_dbus_error('org.gnome.gjs.JSError.ValueError',
                'Service implementation returned an incorrect value type');
        }
    } else if (this[`${methodName}Async`]) {
        const fdList = invocation.get_message().get_unix_fd_list();
        this[`${methodName}Async`](parameters.deepUnpack(), invocation, fdList);
    } else {
        log(`Missing handler for DBus method ${methodName}`);
        invocation.return_gerror(new Gio.DBusError({
            code: Gio.DBusError.UNKNOWN_METHOD,
            message: `Method ${methodName} is not implemented`,
        }));
    }
}

function _handlePropertyGet(info, impl, propertyName) {
    let propInfo = info.lookup_property(propertyName);
    let jsval = this[propertyName];
    if (jsval !== undefined)
        return new GLib.Variant(propInfo.signature, jsval);
    else
        return null;
}

function _handlePropertySet(info, impl, propertyName, newValue) {
    this[propertyName] = newValue.deepUnpack();
}

function _wrapJSObject(interfaceInfo, jsObj) {
    var info;
    if (interfaceInfo instanceof Gio.DBusInterfaceInfo)
        info = interfaceInfo;
    else
        info = Gio.DBusInterfaceInfo.new_for_xml(interfaceInfo);
    info.cache_build();

    var impl = new GjsPrivate.DBusImplementation({g_interface_info: info});
    impl.connect('handle-method-call', function (self, methodName, parameters, invocation) {
        return _handleMethodCall.call(jsObj, info, self, methodName, parameters, invocation);
    });
    impl.connect('handle-property-get', function (self, propertyName) {
        return _handlePropertyGet.call(jsObj, info, self, propertyName);
    });
    impl.connect('handle-property-set', function (self, propertyName, value) {
        return _handlePropertySet.call(jsObj, info, self, propertyName, value);
    });

    return impl;
}

function* _listModelIterator() {
    let _index = 0;
    const _len = this.get_n_items();
    while (_index < _len)
        yield this.get_item(_index++);
}

function _promisify(proto, asyncFunc, finishFunc) {
    if (proto[asyncFunc] === undefined)
        throw new Error(`${proto} has no method named ${asyncFunc}`);

    if (proto[finishFunc] === undefined)
        throw new Error(`${proto} has no method named ${finishFunc}`);

    if (proto[`_original_${asyncFunc}`] !== undefined)
        return;
    proto[`_original_${asyncFunc}`] = proto[asyncFunc];
    proto[asyncFunc] = function (...args) {
        if (!args.every(arg => typeof arg !== 'function'))
            return this[`_original_${asyncFunc}`](...args);
        return new Promise((resolve, reject) => {
            const callStack = new Error().stack.split('\n').filter(line => !line.match(/promisify/)).join('\n');
            this[`_original_${asyncFunc}`](...args, function (source, res) {
                try {
                    const result = source !== null && source[finishFunc] !== undefined
                        ? source[finishFunc](res)
                        : proto[finishFunc](res);
                    if (Array.isArray(result) && result.length > 1 && result[0] === true)
                        result.shift();
                    resolve(result);
                } catch (error) {
                    if (error.stack)
                        error.stack += `### Promise created here: ###\n${callStack}`;
                    else
                        error.stack = callStack;
                    reject(error);
                }
            });
        });
    };
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
        get: Gio.bus_get,
        get_finish: Gio.bus_get_finish,
        get_sync: Gio.bus_get_sync,

        own_name: Gio.bus_own_name,
        own_name_on_connection: Gio.bus_own_name_on_connection,
        unown_name: Gio.bus_unown_name,

        watch_name: Gio.bus_watch_name,
        watch_name_on_connection: Gio.bus_watch_name_on_connection,
        unwatch_name: Gio.bus_unwatch_name,
    };

    Gio.DBusConnection.prototype.watch_name = function (name, flags, appeared, vanished) {
        return Gio.bus_watch_name_on_connection(this, name, flags, appeared, vanished);
    };
    Gio.DBusConnection.prototype.unwatch_name = function (id) {
        return Gio.bus_unwatch_name(id);
    };
    Gio.DBusConnection.prototype.own_name = function (name, flags, acquired, lost) {
        return Gio.bus_own_name_on_connection(this, name, flags, acquired, lost);
    };
    Gio.DBusConnection.prototype.unown_name = function (id) {
        return Gio.bus_unown_name(id);
    };

    _injectToMethod(Gio.DBusProxy.prototype, 'init', _addDBusConvenience);
    _injectToMethod(Gio.DBusProxy.prototype, 'init_async', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_sync', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_finish', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_for_bus_sync', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_for_bus_finish', _addDBusConvenience);
    Gio.DBusProxy.prototype.connectSignal = Signals._connect;
    Gio.DBusProxy.prototype.disconnectSignal = Signals._disconnect;

    Gio.DBusProxy.makeProxyWrapper = _makeProxyWrapper;

    // Some helpers
    _wrapFunction(Gio.DBusNodeInfo, 'new_for_xml', _newNodeInfo);
    Gio.DBusInterfaceInfo.new_for_xml = _newInterfaceInfo;

    Gio.DBusExportedObject = GjsPrivate.DBusImplementation;
    Gio.DBusExportedObject.wrapJSObject = _wrapJSObject;

    // ListStore
    Gio.ListStore.prototype[Symbol.iterator] = _listModelIterator;
    Gio.ListStore.prototype.insert_sorted = function (item, compareFunc) {
        return GjsPrivate.list_store_insert_sorted(this, item, compareFunc);
    };
    Gio.ListStore.prototype.sort = function (compareFunc) {
        return GjsPrivate.list_store_sort(this, compareFunc);
    };

    // Promisify
    Gio._promisify = _promisify;

    // Temporary Gio.File.prototype fix
    Gio._LocalFilePrototype = Gio.File.new_for_path('/').constructor.prototype;

    // Override Gio.Settings and Gio.SettingsSchema - the C API asserts if
    // trying to access a nonexistent schema or key, which is not handy for
    // shell-extension writers

    Gio.SettingsSchema.prototype._realGetKey = Gio.SettingsSchema.prototype.get_key;
    Gio.SettingsSchema.prototype.get_key = function (key) {
        if (!this.has_key(key))
            throw new Error(`GSettings key ${key} not found in schema ${this.get_id()}`);
        return this._realGetKey(key);
    };

    Gio.Settings.prototype._realMethods = Object.assign({}, Gio.Settings.prototype);

    function createCheckedMethod(method, checkMethod = '_checkKey') {
        return function (id, ...args) {
            this[checkMethod](id);
            return this._realMethods[method].call(this, id, ...args);
        };
    }

    Object.assign(Gio.Settings.prototype, {
        _realInit: Gio.Settings.prototype._init,  // add manually, not enumerable
        _init(props = {}) {
            // 'schema' is a deprecated alias for schema_id
            const schemaIdProp = ['schema', 'schema-id', 'schema_id',
                'schemaId'].find(prop => prop in props);
            const settingsSchemaProp = ['settings-schema', 'settings_schema',
                'settingsSchema'].find(prop => prop in props);
            if (!schemaIdProp && !settingsSchemaProp) {
                throw new Error('One of property \'schema-id\' or ' +
                    '\'settings-schema\' are required for Gio.Settings');
            }

            const source = Gio.SettingsSchemaSource.get_default();
            const settingsSchema = settingsSchemaProp
                ? props[settingsSchemaProp]
                : source.lookup(props[schemaIdProp], true);

            if (!settingsSchema)
                throw new Error(`GSettings schema ${props[schemaIdProp]} not found`);

            const settingsSchemaPath = settingsSchema.get_path();
            if (props['path'] === undefined && !settingsSchemaPath) {
                throw new Error('Attempting to create schema ' +
                    `'${settingsSchema.get_id()}' without a path`);
            }

            if (props['path'] !== undefined && settingsSchemaPath &&
                props['path'] !== settingsSchemaPath) {
                throw new Error(`GSettings created for path '${props['path']}'` +
                    `, but schema specifies '${settingsSchemaPath}'`);
            }

            return this._realInit(props);
        },

        _checkKey(key) {
            // Avoid using has_key(); checking a JS array is faster than calling
            // through G-I.
            if (!this._keys)
                this._keys = this.settings_schema.list_keys();

            if (!this._keys.includes(key))
                throw new Error(`GSettings key ${key} not found in schema ${this.schema_id}`);
        },

        _checkChild(name) {
            if (!this._children)
                this._children = this.list_children();

            if (!this._children.includes(name))
                throw new Error(`Child ${name} not found in GSettings schema ${this.schema_id}`);
        },

        get_boolean: createCheckedMethod('get_boolean'),
        set_boolean: createCheckedMethod('set_boolean'),
        get_double: createCheckedMethod('get_double'),
        set_double: createCheckedMethod('set_double'),
        get_enum: createCheckedMethod('get_enum'),
        set_enum: createCheckedMethod('set_enum'),
        get_flags: createCheckedMethod('get_flags'),
        set_flags: createCheckedMethod('set_flags'),
        get_int: createCheckedMethod('get_int'),
        set_int: createCheckedMethod('set_int'),
        get_int64: createCheckedMethod('get_int64'),
        set_int64: createCheckedMethod('set_int64'),
        get_string: createCheckedMethod('get_string'),
        set_string: createCheckedMethod('set_string'),
        get_strv: createCheckedMethod('get_strv'),
        set_strv: createCheckedMethod('set_strv'),
        get_uint: createCheckedMethod('get_uint'),
        set_uint: createCheckedMethod('set_uint'),
        get_uint64: createCheckedMethod('get_uint64'),
        set_uint64: createCheckedMethod('set_uint64'),
        get_value: createCheckedMethod('get_value'),
        set_value: createCheckedMethod('set_value'),

        bind: createCheckedMethod('bind'),
        bind_writable: createCheckedMethod('bind_writable'),
        create_action: createCheckedMethod('create_action'),
        get_default_value: createCheckedMethod('get_default_value'),
        get_user_value: createCheckedMethod('get_user_value'),
        is_writable: createCheckedMethod('is_writable'),
        reset: createCheckedMethod('reset'),

        get_child: createCheckedMethod('get_child', '_checkChild'),
    });
}
