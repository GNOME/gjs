/* -*- mode: js; indent-tabs-mode: nil; -*- */
/* exported Class, Interface, defineGObjectLegacyObjects,
defineGtkLegacyObjects */
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2011 Jasper St. Pierre

// Class magic
// Adapted from MooTools
// https://github.com/mootools/mootools-core

function _Base() {
    throw new TypeError('Cannot instantiate abstract class _Base');
}

_Base.__super__ = null;
_Base.prototype._init = function () { };
_Base.prototype._construct = function (...args) {
    this._init(...args);
    return this;
};
_Base.prototype.__name__ = '_Base';
_Base.prototype.toString = function () {
    return `[object ${this.__name__}]`;
};

function _parent(...args) {
    if (!this.__caller__)
        throw new TypeError("The method 'parent' cannot be called");

    let caller = this.__caller__;
    let name = caller._name;
    let parent = caller._owner.__super__;

    let previous = parent ? parent.prototype[name] : undefined;

    if (!previous)
        throw new TypeError(`The method '${name}' is not on the superclass`);

    return previous.apply(this, args);
}

function _interfacePresent(required, proto) {
    if (!proto.__interfaces__)
        return false;
    if (proto.__interfaces__.indexOf(required) !== -1)
        return true;  // implemented here
    // Might be implemented on a parent class
    return _interfacePresent(required, proto.constructor.__super__.prototype);
}

function getMetaClass(params) {
    if (params.MetaClass)
        return params.MetaClass;

    if (params.Extends && params.Extends.prototype.__metaclass__)
        return params.Extends.prototype.__metaclass__;

    return null;
}

function Class(params, ...otherArgs) {
    let metaClass = getMetaClass(params);

    if (metaClass && metaClass !== this.constructor)
        return new metaClass(params, ...otherArgs);
    else
        return this._construct(params, ...otherArgs);
}

Class.__super__ = _Base;
Class.prototype = Object.create(_Base.prototype);
Class.prototype.constructor = Class;
Class.prototype.__name__ = 'Class';

Class.prototype.wrapFunction = function (name, meth) {
    if (meth._origin)
        meth = meth._origin;

    function wrapper(...args) {
        let prevCaller = this.__caller__;
        this.__caller__ = wrapper;
        let result = meth.apply(this, args);
        this.__caller__ = prevCaller;
        return result;
    }

    wrapper._origin = meth;
    wrapper._name = name;
    wrapper._owner = this;

    return wrapper;
};

Class.prototype.toString = function () {
    return `[object ${this.__name__} for ${this.prototype.__name__}]`;
};

Class.prototype._construct = function (params, ...otherArgs) {
    if (!params.Name)
        throw new TypeError("Classes require an explicit 'Name' parameter.");

    let name = params.Name;

    let parent = params.Extends;
    if (!parent)
        parent = _Base;

    function newClass(...args) {
        if (params.Abstract && new.target.name === name)
            throw new TypeError(`Cannot instantiate abstract class ${name}`);

        this.__caller__ = null;

        return this._construct(...args);
    }

    // Since it's not possible to create a constructor with
    // a custom [[Prototype]], we have to do this to make
    // "newClass instanceof Class" work, and so we can inherit
    // methods/properties of Class.prototype, like wrapFunction.
    Object.setPrototypeOf(newClass, this.constructor.prototype);

    newClass.__super__ = parent;
    newClass.prototype = Object.create(parent.prototype);
    newClass.prototype.constructor = newClass;

    newClass._init(params, ...otherArgs);

    let interfaces = params.Implements || [];
    // If the parent already implements an interface, then we do too
    if (parent instanceof Class)
        interfaces = interfaces.filter(iface => !parent.implements(iface));

    Object.defineProperties(newClass.prototype, {
        '__metaclass__': {
            writable: false,
            configurable: false,
            enumerable: false,
            value: this.constructor,
        },
        '__interfaces__': {
            writable: false,
            configurable: false,
            enumerable: false,
            value: interfaces,
        },
    });
    Object.defineProperty(newClass, 'name', {
        writable: false,
        configurable: true,
        enumerable: false,
        value: name,
    });

    interfaces.forEach(iface => {
        iface._check(newClass.prototype);
    });

    return newClass;
};

/**
 * Check whether this class conforms to the interface "iface".
 *
 * @param {object} iface a Lang.Interface
 * @returns {boolean} whether this class implements iface
 */
Class.prototype.implements = function (iface) {
    if (_interfacePresent(iface, this.prototype))
        return true;
    if (this.__super__ instanceof Class)
        return this.__super__.implements(iface);
    return false;
};

// key can be either a string or a symbol
Class.prototype._copyPropertyDescriptor = function (params, propertyObj, key) {
    let descriptor = Object.getOwnPropertyDescriptor(params, key);

    if (typeof descriptor.value === 'function')
        descriptor.value = this.wrapFunction(key, descriptor.value);

    // we inherit writable and enumerable from the property
    // descriptor of params (they're both true if created from an
    // object literal)
    descriptor.configurable = false;

    propertyObj[key] = descriptor;
};

Class.prototype._init = function (params) {
    let className = params.Name;

    let propertyObj = { };

    let interfaces = params.Implements || [];
    interfaces.forEach(iface => {
        Object.getOwnPropertyNames(iface.prototype)
        .filter(name => !name.startsWith('__') && name !== 'constructor')
        .filter(name => !(name in this.prototype))
        .forEach(name => {
            let descriptor = Object.getOwnPropertyDescriptor(iface.prototype,
                name);
            // writable and enumerable are inherited, see note above
            descriptor.configurable = false;
            propertyObj[name] = descriptor;
        });
    });

    Object.getOwnPropertyNames(params)
    .filter(name =>
        ['Name', 'Extends', 'Abstract', 'Implements'].indexOf(name) === -1)
    .concat(Object.getOwnPropertySymbols(params))
    .forEach(this._copyPropertyDescriptor.bind(this, params, propertyObj));

    Object.defineProperties(this.prototype, propertyObj);
    Object.defineProperties(this.prototype, {
        '__name__': {
            writable: false,
            configurable: false,
            enumerable: false,
            value: className,
        },
        'parent': {
            writable: false,
            configurable: false,
            enumerable: false,
            value: _parent,
        },
    });
};

// This introduces the concept of a "meta-interface" which is given by the
// MetaInterface property on an object's metaclass. For objects whose metaclass
// is Lang.Class, the meta-interface is Lang.Interface. Subclasses of Lang.Class
// such as GObject.Class supply their own meta-interface.
// This is in order to enable creating GObject interfaces with Lang.Interface,
// much as you can create GObject classes with Lang.Class.
function _getMetaInterface(params) {
    if (!params.Requires || params.Requires.length === 0)
        return null;

    let metaInterface = params.Requires.map(req => {
        if (req instanceof Interface)
            return req.__super__;
        for (let metaclass = req.prototype.__metaclass__; metaclass;
            metaclass = metaclass.__super__) {
            if (Object.hasOwn(metaclass, 'MetaInterface'))
                return metaclass.MetaInterface;
        }
        return null;
    })
    .reduce((best, candidate) => {
        // This function reduces to the "most derived" meta interface in the list.
        if (best === null)
            return candidate;
        if (candidate === null)
            return best;
        for (let sup = candidate; sup; sup = sup.__super__) {
            if (sup === best)
                return candidate;
        }
        return best;
    }, null);

    // If we reach this point and we don't know the meta-interface, then it's
    // most likely because there were only pure-C interfaces listed in Requires
    // (and those don't have our magic properties.) However, all pure-C
    // interfaces should require GObject.Object anyway.
    if (metaInterface === null)
        throw new Error('Did you forget to include GObject.Object in Requires?');

    return metaInterface;
}

function Interface(params, ...otherArgs) {
    let metaInterface = _getMetaInterface(params);
    if (metaInterface && metaInterface !== this.constructor)
        return new metaInterface(params, ...otherArgs);
    return this._construct(params, ...otherArgs);
}

Class.MetaInterface = Interface;

/**
 * Use this to signify a function that must be overridden in an implementation
 * of the interface. Creating a class that doesn't override the function will
 * throw an error.
 */
Interface.UNIMPLEMENTED = function UNIMPLEMENTED() {
    throw new Error('Not implemented');
};

Interface.__super__ = _Base;
Interface.prototype = Object.create(_Base.prototype);
Interface.prototype.constructor = Interface;
Interface.prototype.__name__ = 'Interface';

Interface.prototype._construct = function (params, ...otherArgs) {
    if (!params.Name)
        throw new TypeError("Interfaces require an explicit 'Name' parameter.");

    let newInterface = Object.create(this.constructor.prototype);

    newInterface.__super__ = Interface;
    newInterface.prototype = Object.create(Interface.prototype);
    newInterface.prototype.constructor = newInterface;
    newInterface.prototype.__name__ = params.Name;

    newInterface._init(params, ...otherArgs);

    Object.defineProperty(newInterface.prototype, '__metaclass__', {
        writable: false,
        configurable: false,
        enumerable: false,
        value: this.constructor,
    });
    Object.defineProperty(newInterface, 'name', {
        writable: false,
        configurable: true,
        enumerable: false,
        value: params.Name,
    });

    return newInterface;
};

Interface.prototype._check = function (proto) {
    // Check that proto implements all of this interface's required interfaces.
    // "proto" refers to the object's prototype (which implements the interface)
    // whereas "this.prototype" is the interface's prototype (which may still
    // contain unimplemented methods.)

    let unfulfilledReqs = this.prototype.__requires__.filter(required => {
        // Either the interface is not present or it is not listed before the
        // interface that requires it or the class does not inherit it. This is
        // so that required interfaces don't copy over properties from other
        // interfaces that require them.
        let interfaces = proto.__interfaces__;
        return (!_interfacePresent(required, proto) ||
            interfaces.indexOf(required) > interfaces.indexOf(this)) &&
            !(proto instanceof required);
    }).map(required =>
        // __name__ is only present on GJS-created classes and will be the most
        // accurate name. required.name will be present on introspected GObjects
        // but is not preferred because it will be the C name. The last option
        // is just so that we print something if there is garbage in Requires.
        required.prototype.__name__ || required.name || required);
    if (unfulfilledReqs.length > 0) {
        throw new Error(`The following interfaces must be implemented before ${
            this.prototype.__name__}: ${unfulfilledReqs.join(', ')}`);
    }

    // Check that this interface's required methods are implemented
    let unimplementedFns = Object.getOwnPropertyNames(this.prototype)
    .filter(p => this.prototype[p] === Interface.UNIMPLEMENTED)
    .filter(p => !(p in proto) || proto[p] === Interface.UNIMPLEMENTED);
    if (unimplementedFns.length > 0) {
        throw new Error(`The following members of ${
            this.prototype.__name__} are not implemented yet: ${
            unimplementedFns.join(', ')}`);
    }
};

Interface.prototype.toString = function () {
    return `[interface ${this.__name__} for ${this.prototype.__name__}]`;
};

Interface.prototype._init = function (params) {
    let ifaceName = params.Name;

    let propertyObj = {};
    Object.getOwnPropertyNames(params)
    .filter(name => ['Name', 'Requires'].indexOf(name) === -1)
    .forEach(name => {
        let descriptor = Object.getOwnPropertyDescriptor(params, name);

        // Create wrappers on the interface object so that generics work (e.g.
        // SomeInterface.some_function(this, blah) instead of
        // SomeInterface.prototype.some_function.call(this, blah)
        if (typeof descriptor.value === 'function') {
            let interfaceProto = this.prototype;  // capture in closure
            this[name] = function (thisObj, ...args) {
                return interfaceProto[name].call(thisObj, ...args);
            };
        }

        // writable and enumerable are inherited, see note in Class._init()
        descriptor.configurable = false;

        propertyObj[name] = descriptor;
    });

    Object.defineProperties(this.prototype, propertyObj);
    Object.defineProperties(this.prototype, {
        '__name__': {
            writable: false,
            configurable: false,
            enumerable: false,
            value: ifaceName,
        },
        '__requires__': {
            writable: false,
            configurable: false,
            enumerable: false,
            value: params.Requires || [],
        },
    });
};

// GObject Lang.Class magic

function defineGObjectLegacyObjects(GObject) {
    const Gi = imports._gi;
    const {_checkAccessors} = imports._common;

    // Some common functions between GObject.Class and GObject.Interface

    function _createSignals(gtype, signals) {
        for (let signalName in signals) {
            let obj = signals[signalName];
            let flags = obj.flags !== undefined ? obj.flags : GObject.SignalFlags.RUN_FIRST;
            let accumulator = obj.accumulator !== undefined ? obj.accumulator : GObject.AccumulatorType.NONE;
            let rtype = obj.return_type !== undefined ? obj.return_type : GObject.TYPE_NONE;
            let paramtypes = obj.param_types !== undefined ? obj.param_types : [];

            try {
                obj.signal_id = Gi.signal_new(gtype, signalName, flags, accumulator, rtype, paramtypes);
            } catch (e) {
                throw new TypeError(`Invalid signal ${signalName}: ${e.message}`);
            }
        }
    }

    function _createGTypeName(params) {
        if (params.GTypeName)
            return params.GTypeName;
        else
            return `Gjs_${params.Name.replace(/[^a-z0-9_+-]/gi, '_')}`;
    }

    function _getGObjectInterfaces(interfaces) {
        return interfaces.filter(iface => Object.hasOwn(iface, '$gtype'));
    }

    function _propertiesAsArray(params) {
        let propertiesArray = [];
        if (params.Properties) {
            for (let prop in params.Properties)
                propertiesArray.push(params.Properties[prop]);
        }
        return propertiesArray;
    }

    const GObjectMeta = new Class({
        Name: 'GObjectClass',
        Extends: Class,

        _init(params) {
            // retrieve signals and remove them from params before chaining
            let signals = params.Signals;
            delete params.Signals;

            this.parent(params);

            if (signals)
                _createSignals(this.$gtype, signals);

            Object.getOwnPropertyNames(params).forEach(name => {
                if (['Name', 'Extends', 'Abstract'].includes(name))
                    return;

                let descriptor = Object.getOwnPropertyDescriptor(params, name);

                if (typeof descriptor.value === 'function') {
                    let wrapped = this.prototype[name];

                    if (name.startsWith('vfunc_')) {
                        this.prototype[Gi.hook_up_vfunc_symbol](name.slice(6),
                            wrapped);
                    } else if (name.startsWith('on_')) {
                        let id = GObject.signal_lookup(name.slice(3).replace('_', '-'), this.$gtype);
                        if (id !== 0) {
                            GObject.signal_override_class_closure(id, this.$gtype, function (...argArray) {
                                let emitter = argArray.shift();

                                return wrapped.apply(emitter, argArray);
                            });
                        }
                    }
                }
            });
        },

        _isValidClass(klass) {
            let proto = klass.prototype;

            if (!proto)
                return false;

            // If proto === GObject.Object.prototype, then
            // proto.__proto__ is Object, so "proto instanceof GObject.Object"
            // will return false.
            return proto === GObject.Object.prototype ||
                proto instanceof GObject.Object;
        },

        // If we want an object with a custom JSClass, we can't just
        // use a function. We have to use a custom constructor here.
        _construct(params, ...otherArgs) {
            if (!params.Name)
                throw new TypeError("Classes require an explicit 'Name' parameter.");
            let name = params.Name;

            let gtypename = _createGTypeName(params);
            let gflags = params.Abstract ? GObject.TypeFlags.ABSTRACT : 0;

            if (!params.Extends)
                params.Extends = GObject.Object;
            let parent = params.Extends;

            if (!this._isValidClass(parent))
                throw new TypeError(`GObject.Class used with invalid base class (is ${parent})`);

            let interfaces = params.Implements || [];
            if (parent instanceof Class)
                interfaces = interfaces.filter(iface => !parent.implements(iface));
            let gobjectInterfaces = _getGObjectInterfaces(interfaces);

            let propertiesArray = _propertiesAsArray(params);
            delete params.Properties;

            propertiesArray.forEach(pspec => _checkAccessors(params, pspec, GObject));

            let newClass = Gi.register_type(parent.prototype, gtypename,
                gflags, gobjectInterfaces, propertiesArray);

            // See Class.prototype._construct for the reasoning
            // behind this direct prototype set.
            Object.setPrototypeOf(newClass, this.constructor.prototype);
            newClass.__super__ = parent;

            newClass._init(params, ...otherArgs);

            Object.defineProperties(newClass.prototype, {
                '__metaclass__': {
                    writable: false,
                    configurable: false,
                    enumerable: false,
                    value: this.constructor,
                },
                '__interfaces__': {
                    writable: false,
                    configurable: false,
                    enumerable: false,
                    value: interfaces,
                },
            });
            // Overwrite the C++-set class name, as if it were an ES6 class
            Object.defineProperty(newClass, 'name', {
                writable: false,
                configurable: true,
                enumerable: false,
                value: name,
            });

            interfaces.forEach(iface => {
                if (iface instanceof Interface)
                    iface._check(newClass.prototype);
            });

            return newClass;
        },

        // Overrides Lang.Class.implements()
        implements(iface) {
            if (iface instanceof GObject.Interface)
                return GObject.type_is_a(this.$gtype, iface.$gtype);
            else
                return this.parent(iface);
        },
    });

    function GObjectInterface(...args) {
        return this._construct(...args);
    }

    GObjectMeta.MetaInterface = GObjectInterface;

    GObjectInterface.__super__ = Interface;
    GObjectInterface.prototype = Object.create(Interface.prototype);
    GObjectInterface.prototype.constructor = GObjectInterface;
    GObjectInterface.prototype.__name__ = 'GObjectInterface';

    GObjectInterface.prototype._construct = function (params, ...otherArgs) {
        if (!params.Name)
            throw new TypeError("Interfaces require an explicit 'Name' parameter.");

        let gtypename = _createGTypeName(params);
        delete params.GTypeName;

        let interfaces = params.Requires || [];
        let gobjectInterfaces = _getGObjectInterfaces(interfaces);

        let properties = _propertiesAsArray(params);
        delete params.Properties;

        let newInterface = Gi.register_interface(gtypename, gobjectInterfaces,
            properties);

        // See Class.prototype._construct for the reasoning
        // behind this direct prototype set.
        Object.setPrototypeOf(newInterface, this.constructor.prototype);
        newInterface.__super__ = GObjectInterface;
        newInterface.prototype.constructor = newInterface;

        newInterface._init(params, ...otherArgs);

        Object.defineProperty(newInterface.prototype, '__metaclass__', {
            writable: false,
            configurable: false,
            enumerable: false,
            value: this.constructor,
        });
        // Overwrite the C++-set class name, as if it were an ES6 class
        Object.defineProperty(newInterface, 'name', {
            writable: false,
            configurable: true,
            enumerable: false,
            value: params.Name,
        });

        return newInterface;
    };

    GObjectInterface.prototype._init = function (params) {
        let signals = params.Signals;
        delete params.Signals;

        Interface.prototype._init.call(this, params);

        _createSignals(this.$gtype, signals);
    };

    return {GObjectMeta, GObjectInterface};
}

function defineGtkLegacyObjects(GObject, Gtk) {
    const {_createBuilderConnectFunc} = imports._common;

    const GtkWidgetClass = new Class({
        Name: 'GtkWidgetClass',
        Extends: GObject.Class,

        _init(params) {
            let template = params.Template;
            delete params.Template;

            let children = params.Children;
            delete params.Children;

            let internalChildren = params.InternalChildren;
            delete params.InternalChildren;

            let cssName = params.CssName;
            delete params.CssName;

            if (template) {
                params._instance_init = function () {
                    this.init_template();
                };
            }

            this.parent(params);

            if (cssName)
                Gtk.Widget.set_css_name.call(this, cssName);

            if (template) {
                if (typeof template === 'string' &&
                    template.startsWith('resource:///'))
                    Gtk.Widget.set_template_from_resource.call(this, template.slice(11));
                else
                    Gtk.Widget.set_template.call(this, template);
            }

            Gtk.Widget.set_connect_func.call(this, _createBuilderConnectFunc(this));

            this[Gtk.template] = template;
            this[Gtk.children] = children;
            this[Gtk.internalChildren] = internalChildren;

            if (children) {
                for (let i = 0; i < children.length; i++)
                    Gtk.Widget.bind_template_child_full.call(this, children[i], false, 0);
            }

            if (internalChildren) {
                for (let i = 0; i < internalChildren.length; i++)
                    Gtk.Widget.bind_template_child_full.call(this, internalChildren[i], true, 0);
            }
        },

        _isValidClass(klass) {
            let proto = klass.prototype;

            if (!proto)
                return false;

            // If proto === Gtk.Widget.prototype, then
            // proto.__proto__ is GObject.InitiallyUnowned, so
            // "proto instanceof Gtk.Widget"
            // will return false.
            return proto === Gtk.Widget.prototype ||
                proto instanceof Gtk.Widget;
        },
    });

    return {GtkWidgetClass};
}
