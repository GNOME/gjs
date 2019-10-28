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

/** @type {Object.<string, any>} */
var module = {};

/**
 * @param {string} ns
 */
let $import = (ns) => imports[ns];

/** @type {any} */
let GjsPrivate;
/** @type {any} */
let GLib;
/** @type {any} */
let Signals;
/** @type {(s: String) => void} */
let log;
/** @type {(error: Error, s: String) => void} */
let logError;

/** @type {any} */
var Gio;

// Ensures that a Gio.UnixFDList being passed into or out of a DBus method with
// a parameter type that includes 'h' somewhere, actually has entries in it for
// each of the indices being passed as an 'h' parameter.
/**
 * @param {any} variant
 * @param {any} fdList
 * @returns {void}
 */
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

/**
 * @param {any} methodName
 * @param {any} sync
 * @param {any} inSignature
 * @param {any[]} argArray
 */
function _proxyInvoker(methodName, sync, inSignature, argArray) {
    // TODO
    /** @type {{ (result: any, exc: any): void; (arg0: any, arg1: any, arg2: any): void; (arg0: any[], arg1: any, arg2: any): void; }} */
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

    /**
     * @param {any} proxy
     * @param {any} result
     */
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

/**
 * @param {any} _result
 * @param {any} exc
 */
function _logReply(_result, exc) {
    if (exc !== null) {
        // TODO printing
        log(`Ignored exception from dbus method: ${exc}`);
    }
}

/**
 * @param {{ name: any; in_args: any; }} method
 * @param {boolean} sync
 */
function _makeProxyMethod(method, sync) {
    var i;
    var name = method.name;
    var inArgs = method.in_args;
    /** @type {any[]} */
    var inSignature = [];
    for (i = 0; i < inArgs.length; i++)
        inSignature.push(inArgs[i].signature);

    return function (/** @type {any[]} */...args) {
        return _proxyInvoker.call(this, name, sync, inSignature, args);
    };
}

/**
 * @param {any} proxy
 * @param {any} senderName
 * @param {any} signalName
 * @param {{ deepUnpack: () => void; }} parameters
 */
function _convertToNativeSignal(proxy, senderName, signalName, parameters) {
    Signals._emit.call(proxy, signalName, senderName, parameters.deepUnpack());
}

/**
 * @param {any} name
 */
function _propertyGetter(name) {
    let value = this.get_cached_property(name);
    return value ? value.deepUnpack() : null;
}

/**
 * @param {any} name
 * @param {any} signature
 * @param {any} value
 */
function _propertySetter(name, signature, value) {
    let variant = new GLib.Variant(signature, value);
    this.set_cached_property(name, variant);

    this.call('org.freedesktop.DBus.Properties.Set',
        new GLib.Variant('(ssv)', [this.g_interface_name, name, variant]),
        Gio.DBusCallFlags.NONE, -1, null,

        (/** @type {any} */ _proxy, /** @type {any} */ result) => {
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



/**
 * @param {any} interfaceXml
 */
function _makeProxyWrapper(interfaceXml) {
    var info = _newInterfaceInfo(interfaceXml);
    var iname = info.name;

    /**
     * @param {any} bus
     * @param {any} name
     * @param {any} object
     * @param {any} asyncCallback
     * @param {any} cancellable
     */
    const wrapper = function (bus, name, object, asyncCallback, cancellable, flags = Gio.DBusProxyFlags.NONE) {
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
            obj.init_async(GLib.PRIORITY_DEFAULT, cancellable,
                /**
                 * @param {any} initable
                 * @param {any} result
                 */
                (initable, result) => {
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

    return wrapper;
}


/**
 * @param {(arg0: string) => void} constructor
 * @param {any} value
 */
function _newNodeInfo(constructor, value) {
    if (typeof value === 'string')
        return constructor(value);
    throw TypeError(`Invalid type ${Object.prototype.toString.call(value)}`);
}

/**
 * @param {any} value
 */
function _newInterfaceInfo(value) {
    var nodeInfo = Gio.DBusNodeInfo.new_for_xml(value);
    return nodeInfo.interfaces[0];
}

/**
 * @param {{ [x: string]: (...args: any[]) => any; }} klass
 * @param {string} method
 * @param {{ (): void; (): void; apply?: any; }} addition
 */
function _injectToMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = /**
     * @param {any} args
     */
        function (...args) {
            addition.apply(this, args);
            return previous.apply(this, args);
        };
}

/**
 * @param {{ [x: string]: (...parameters: any[]) => any; }} klass
 * @param {string} method
 * @param {{ (): void; (): void; (): void; (): void; apply?: any; }} addition
 */
function _injectToStaticMethod(klass, method, addition) {
    var previous = klass[method];

    klass[method] = /**
     * @param {any} parameters
     */
        function (...parameters) {
            let obj = previous.apply(this, parameters);
            addition.apply(obj, parameters);
            return obj;
        };
}

/**
 * @param {{ [x: string]: (...args: any[]) => any; }} klass
 * @param {string} method
 * @param {{ (constructor: any, value: any): any; apply?: any; }} addition
 */
function _wrapFunction(klass, method, addition) {
    var previous = klass[method];

    /**
     * @param {any[]} args
     */
    klass[method] = function (...args) {
        args.unshift(previous);
        return addition.apply(this, args);
    };
}

/**
 * @param {{ signature: string; }[]} args
 */
function _makeOutSignature(args) {
    var ret = '(';
    for (var i = 0; i < args.length; i++)
        ret += args[i].signature;

    return `${ret})`;
}

/**
 * @param {any} info
 * @param {any} _impl
 * @param {string | number} methodName
 * @param {any} parameters
 * @param {any} invocation
 */
function _handleMethodCall(info, _impl, methodName, parameters, invocation) {
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

/**
 * @param {any} info
 * @param {any} _impl
 * @param {string | number} propertyName
 */
function _handlePropertyGet(info, _impl, propertyName) {
    let propInfo = info.lookup_property(propertyName);
    let jsval = this[propertyName];
    if (jsval !== undefined)
        return new GLib.Variant(propInfo.signature, jsval);
    else
        return null;
}

/**
 * @param {any} _info
 * @param {any} _impl
 * @param {string | number} propertyName
 * @param {any} newValue
 */
function _handlePropertySet(_info, _impl, propertyName, newValue) {
    this[propertyName] = newValue.deepUnpack();
}

/**
 * @param {any} interfaceInfo
 * @param {any} jsObj
 */
function _wrapJSObject(interfaceInfo, jsObj) {
    /** @type {{ cache_build: () => void; }} */
    var info;
    if (interfaceInfo instanceof Gio.DBusInterfaceInfo)
        info = interfaceInfo;
    else
        info = Gio.DBusInterfaceInfo.new_for_xml(interfaceInfo);
    info.cache_build();

    var impl = new GjsPrivate.DBusImplementation({ g_interface_info: info });
    impl.connect('handle-method-call', /**
         * @param {any} self
         * @param {any} methodName
         * @param {any} parameters
         * @param {any} invocation
         */
        function (self, methodName, parameters, invocation) {
            return _handleMethodCall.call(jsObj, info, self, methodName, parameters, invocation);
        });
    impl.connect('handle-property-get', /**
         * @param {any} self
         * @param {any} propertyName
         */
        function (self, propertyName) {
            return _handlePropertyGet.call(jsObj, info, self, propertyName);
        });
    impl.connect('handle-property-set', /**
         * @param {any} self
         * @param {any} propertyName
         * @param {any} value
         */
        function (self, propertyName, value) {
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

/**
 * @param {{ [x: string]: (...args: any[]) => any; }} proto
 * @param {string | number} asyncFunc
 * @param {string | number} finishFunc
 */
function _promisify(proto, asyncFunc, finishFunc) {
    proto[`_original_${asyncFunc}`] = proto[asyncFunc];
    /**
     * @param {any[]} args
     */
    proto[asyncFunc] = function (...args) {
        if (!args.every(arg => typeof arg !== 'function'))
            return this[`_original_${asyncFunc}`](...args);
        return new Promise((resolve, reject) => {
            const callStack = new Error().stack.split('\n').filter(line => !line.match(/promisify/)).join('\n');

            this[`_original_${asyncFunc}`](...args,
                /**
                 * @param {any} source
                 * @param {any} res
                 */
                function (source, res) {
                    try {
                        const result = source[finishFunc](res);
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

/**
 * @param {(ns: string) => any} require
 */
function _init(require) {
    if (require) {
        $import = require;
    }

    const gi = $import('gi');
    GjsPrivate = gi.GjsPrivate, GLib = gi.GLib;

    Signals = $import('_signals');

    const print = $import('print');
    logError = print.logError;
    log = print.log;

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

    Gio.DBusConnection.prototype.watch_name = /**
     * @param {any} name
     * @param {any} flags
     * @param {any} appeared
     * @param {any} vanished
     */
        function (name, flags, appeared, vanished) {
            return Gio.bus_watch_name_on_connection(this, name, flags, appeared, vanished);
        };
    /**
     * @param {any} id
     */
    Gio.DBusConnection.prototype.unwatch_name = function (id) {
        return Gio.bus_unwatch_name(id);
    };
    /**
     * @param {any} name
     * @param {any} flags
     * @param {any} acquired
     * @param {any} lost
     */
    Gio.DBusConnection.prototype.own_name = function (name, flags, acquired, lost) {
        return Gio.bus_own_name_on_connection(this, name, flags, acquired, lost);
    };
    /**
     * @param {any} id
     */
    Gio.DBusConnection.prototype.unown_name = function (id) {
        return Gio.bus_unown_name(id);
    };

    _injectToMethod(Gio.DBusProxy.prototype, 'init', _addDBusConvenience);
    _injectToMethod(Gio.DBusProxy.prototype, 'init_async', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_sync', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_finish', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_for_bus_sync', _addDBusConvenience);
    _injectToStaticMethod(Gio.DBusProxy, 'new_for_bus_finish', _addDBusConvenience);

    // TODO Figure out how to "neutralize the signals implementation"
    if (typeof Signals !== 'undefined') {
        Gio.DBusProxy.prototype.connectSignal = Signals._connect;
        Gio.DBusProxy.prototype.disconnectSignal = Signals._disconnect;
    }

    Gio.DBusProxy.makeProxyWrapper = _makeProxyWrapper;

    // Some helpers
    _wrapFunction(Gio.DBusNodeInfo, 'new_for_xml', _newNodeInfo);
    Gio.DBusInterfaceInfo.new_for_xml = _newInterfaceInfo;

    Gio.DBusExportedObject = GjsPrivate.DBusImplementation || {}; // TODO This is broken in module mode for some reason
    Gio.DBusExportedObject.wrapJSObject = _wrapJSObject;

    // ListStore
    Gio.ListStore.prototype[Symbol.iterator] = _listModelIterator;

    // Promisify
    Gio._promisify = _promisify;

    // Temporary Gio.File.prototype fix
    Gio._LocalFilePrototype = Gio.File.new_for_path('').constructor.prototype;

    // Override Gio.Settings and Gio.SettingsSchema - the C API asserts if
    // trying to access a nonexistent schema or key, which is not handy for
    // shell-extension writers

    Gio.SettingsSchema.prototype._realGetKey = Gio.SettingsSchema.prototype.get_key;
    /**
     * @param {any} key
     */
    Gio.SettingsSchema.prototype.get_key = function (key) {
        if (!this.has_key(key))
            throw new Error(`GSettings key ${key} not found in schema ${this.get_id()}`);
        return this._realGetKey(key);
    };

    Gio.Settings.prototype._realMethods = Object.assign({}, Gio.Settings.prototype);

    /**
     * @param {string} method
     */
    function createCheckedMethod(method, checkMethod = '_checkKey') {
        return function (
            /** @type {any} */ id,
            /** @type {any[]} */ ...args
        ) {
            this[checkMethod](id);
            return this._realMethods[method].call(this, id, ...args);
        };
    }

    Object.assign(Gio.Settings.prototype, {
        _realInit: Gio.Settings.prototype._init,  // add manually, not enumerable
        /**
         * @param {Object.<string, any>} props 
         */
        _init(props = {}) {
            // 'schema' is a deprecated alias for schema_id
            const requiredProps = ['schema', 'schema-id', 'schema_id', 'schemaId',
                'settings-schema', 'settings_schema', 'settingsSchema'];
            if (requiredProps.every(prop => !(prop in props))) {
                throw new Error('One of property \'schema-id\' or ' +
                    '\'settings-schema\' are required for Gio.Settings');
            }

            const checkSchemasProps = ['schema', 'schema-id', 'schema_id', 'schemaId'];
            const source = Gio.SettingsSchemaSource.get_default();
            for (const prop of checkSchemasProps) {
                if (!(prop in props))
                    continue;
                if (source.lookup(props[prop], true) === null)
                    throw new Error(`GSettings schema ${props[prop]} not found`);
            }

            return this._realInit(props);
        },

        /**
         * @param {any} key
         */
        _checkKey(key) {
            // Avoid using has_key(); checking a JS array is faster than calling
            // through G-I.
            // @ts-ignore TODO: Type definitions are a problem here.
            if (!this._keys)
                // @ts-ignore TODO: Type definitions are a problem here.
                this._keys = this.settings_schema.list_keys();

            // @ts-ignore TODO: Type definitions are a problem here.
            if (!this._keys.includes(key))
                // @ts-ignore TODO: Type definitions are a problem here.
                throw new Error(`GSettings key ${key} not found in schema ${this.schema_id}`);
        },

        /**
         * @param {any} name
         */
        _checkChild(name) {
            // @ts-ignore TODO: Type definitions are a problem here.
            if (!this._children)
                // @ts-ignore TODO: Type definitions are a problem here.
                this._children = this.list_children();

            // @ts-ignore TODO: Type definitions are a problem here.
            if (!this._children.includes(name))
                // @ts-ignore TODO: Type definitions are a problem here.
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

module.exports = {
    _init
}