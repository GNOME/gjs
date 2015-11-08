/* -*- mode: js; indent-tabs-mode: nil; -*- */
// Copyright (c) 2008  litl, LLC
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

// Utilities that are "meta-language" things like manipulating object props

const Gi = imports._gi;

function countProperties(obj) {
    let count = 0;
    for (let property in obj) {
        count += 1;
    }
    return count;
}

function getPropertyDescriptor(obj, property) {
    if (obj.hasOwnProperty(property))
        return Object.getOwnPropertyDescriptor(obj, property);
    return getPropertyDescriptor(Object.getPrototypeOf(obj), property);
}

function _copyProperty(source, dest, property) {
    let descriptor = getPropertyDescriptor(source, property);
    Object.defineProperty(dest, property, descriptor);
}

function copyProperties(source, dest) {
    for (let property in source) {
        _copyProperty(source, dest, property);
    }
}

function copyPublicProperties(source, dest) {
    for (let property in source) {
        if (typeof(property) == 'string' &&
            property.substring(0, 1) == '_') {
            continue;
        } else {
            _copyProperty(source, dest, property);
        }
    }
}

/**
 * Binds obj to callback. Makes it possible to refer to "obj"
 * using this within the callback.
 * @param {object} obj the object to bind
 * @param {function} callback callback to bind obj in
 * @param arguments additional arguments to the callback
 * @returns: a new callback
 * @type: function
 */
function bind(obj, callback) {
    if (typeof(obj) != 'object') {
        throw new Error(
            "first argument to Lang.bind() must be an object, not " +
                typeof(obj));
    }

    if (typeof(callback) != 'function') {
        throw new Error(
            "second argument to Lang.bind() must be a function, not " +
                typeof(callback));
    }

    // Use ES5 Function.prototype.bind, but only if not passing any bindArguments,
    // because ES5 has them at the beginning, not at the end
    if (arguments.length == 2)
	return callback.bind(obj);

    let me = obj;
    let bindArguments = Array.prototype.slice.call(arguments, 2);

    return function() {
        let args = Array.prototype.slice.call(arguments);
        args = args.concat(bindArguments);
        return callback.apply(me, args);
    };
}

// Class magic
// Adapted from MooTools, MIT license
// https://github.com/mootools/mootools-core

function _Base() {
    throw new TypeError('Cannot instantiate abstract class _Base');
}

_Base.__super__ = null;
_Base.prototype._init = function() { };
_Base.prototype._construct = function() {
    this._init.apply(this, arguments);
    return this;
};
_Base.prototype.__name__ = '_Base';
_Base.prototype.toString = function() {
    return '[object ' + this.__name__ + ']';
};

function _parent() {
    if (!this.__caller__)
        throw new TypeError("The method 'parent' cannot be called");

    let caller = this.__caller__;
    let name = caller._name;
    let parent = caller._owner.__super__;

    let previous = parent ? parent.prototype[name] : undefined;

    if (!previous)
        throw new TypeError("The method '" + name + "' is not on the superclass");

    return previous.apply(this, arguments);
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

function Class(params) {
    let metaClass = getMetaClass(params);

    if (metaClass && metaClass != this.constructor) {
        // Trick to apply variadic arguments to constructors --
        // bind the arguments into the constructor function.
        let args = Array.prototype.slice.call(arguments);
        let curried = Function.prototype.bind.apply(metaClass, [,].concat(args));
        return new curried();
    } else {
        return this._construct.apply(this, arguments);
    }
}

Class.__super__ = _Base;
Class.prototype = Object.create(_Base.prototype);
Class.prototype.constructor = Class;
Class.prototype.__name__ = 'Class';

Class.prototype.wrapFunction = function(name, meth) {
    if (meth._origin) meth = meth._origin;

    function wrapper() {
        let prevCaller = this.__caller__;
        this.__caller__ = wrapper;
        let result = meth.apply(this, arguments);
        this.__caller__ = prevCaller;
        return result;
    }

    wrapper._origin = meth;
    wrapper._name = name;
    wrapper._owner = this;

    return wrapper;
}

Class.prototype.toString = function() {
    return '[object ' + this.__name__ + ' for ' + this.prototype.__name__ + ']';
};

Class.prototype._construct = function(params) {
    if (!params.Name) {
        throw new TypeError("Classes require an explicit 'Name' parameter.");
    }
    let name = params.Name;

    let parent = params.Extends;
    if (!parent)
        parent = _Base;

    let newClass;
    if (params.Abstract) {
        newClass = function() {
            throw new TypeError('Cannot instantiate abstract class ' + name);
        };
    } else {
        newClass = function() {
            this.__caller__ = null;

            return this._construct.apply(this, arguments);
        };
    }

    // Since it's not possible to create a constructor with
    // a custom [[Prototype]], we have to do this to make
    // "newClass instanceof Class" work, and so we can inherit
    // methods/properties of Class.prototype, like wrapFunction.
    newClass.__proto__ = this.constructor.prototype;

    newClass.__super__ = parent;
    newClass.prototype = Object.create(parent.prototype);
    newClass.prototype.constructor = newClass;

    newClass._init.apply(newClass, arguments);

    let interfaces = params.Implements || [];
    // If the parent already implements an interface, then we do too
    if (parent instanceof Class)
        interfaces = interfaces.filter((iface) => !parent.implements(iface));

    Object.defineProperties(newClass.prototype, {
        '__metaclass__': { writable: false,
                           configurable: false,
                           enumerable: false,
                           value: this.constructor },
        '__interfaces__': { writable: false,
                            configurable: false,
                            enumerable: false,
                            value: interfaces }
    });

    interfaces.forEach((iface) => {
        iface._check(newClass.prototype);
    });

    return newClass;
};

/**
 * Check whether this class conforms to the interface "iface".
 * @param {object} iface a Lang.Interface
 * @returns: whether this class implements iface
 * @type: boolean
 */
Class.prototype.implements = function (iface) {
    if (_interfacePresent(iface, this.prototype))
        return true;
    if (this.__super__ instanceof Class)
        return this.__super__.implements(iface);
    return false;
};

Class.prototype._init = function(params) {
    let name = params.Name;

    let propertyObj = { };

    let interfaces = params.Implements || [];
    interfaces.forEach((iface) => {
        Object.getOwnPropertyNames(iface.prototype)
        .filter((name) => !name.startsWith('__') && name !== 'constructor')
        .forEach((name) => {
            let descriptor = Object.getOwnPropertyDescriptor(iface.prototype,
                name);
            // writable and enumerable are inherited, see note below
            descriptor.configurable = false;
            propertyObj[name] = descriptor;
        });
    });

    Object.getOwnPropertyNames(params).forEach(function(name) {
        if (['Name', 'Extends', 'Abstract', 'Implements'].indexOf(name) !== -1)
            return;

        let descriptor = Object.getOwnPropertyDescriptor(params, name);

        if (typeof descriptor.value === 'function')
            descriptor.value = this.wrapFunction(name, descriptor.value);

        // we inherit writable and enumerable from the property
        // descriptor of params (they're both true if created from an
        // object literal)
        descriptor.configurable = false;

        propertyObj[name] = descriptor;
    }.bind(this));

    Object.defineProperties(this.prototype, propertyObj);
    Object.defineProperties(this.prototype, {
        '__name__': { writable: false,
                      configurable: false,
                      enumerable: false,
                      value: name },
        'parent': { writable: false,
                    configurable: false,
                    enumerable: false,
                    value: _parent }});
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

    let metaInterface = params.Requires.map((req) => {
        if (req instanceof Interface)
            return req.__super__;
        for (let metaclass = req.prototype.__metaclass__; metaclass;
            metaclass = metaclass.__super__) {
            if (metaclass.hasOwnProperty('MetaInterface'))
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

function Interface(params) {
    let metaInterface = _getMetaInterface(params);
    if (metaInterface && metaInterface !== this.constructor) {
        // Trick to apply variadic arguments to constructors --
        // bind the arguments into the constructor function.
        let args = Array.prototype.slice.call(arguments);
        let curried = Function.prototype.bind.apply(metaInterface, [,].concat(args));
        return new curried();
    }
    return this._construct.apply(this, arguments);
}

Class.MetaInterface = Interface;

/**
 * Use this to signify a function that must be overridden in an implementation
 * of the interface. Creating a class that doesn't override the function will
 * throw an error.
 */
Interface.UNIMPLEMENTED = function () {
    throw new Error('Not implemented');
};

Interface.__super__ = _Base;
Interface.prototype = Object.create(_Base.prototype);
Interface.prototype.constructor = Interface;
Interface.prototype.__name__ = 'Interface';

Interface.prototype._construct = function (params) {
    if (!params.Name)
        throw new TypeError("Interfaces require an explicit 'Name' parameter.");
    let name = params.Name;

    let newInterface = function () {
        throw new TypeError('Cannot instantiate interface ' + name);
    };

    // See note in Class._construct(); this makes "newInterface instanceof
    // Interface" work, and allows inheritance.
    newInterface.__proto__ = this.constructor.prototype;

    newInterface.__super__ = Interface;
    newInterface.prototype = Object.create(Interface.prototype);
    newInterface.prototype.constructor = newInterface;

    newInterface._init.apply(newInterface, arguments);

    Object.defineProperty(newInterface.prototype, '__metaclass__',
                          { writable: false,
                            configurable: false,
                            enumerable: false,
                            value: this.constructor });

    return newInterface;
};

Interface.prototype._check = function (proto) {
    // Check that proto implements all of this interface's required interfaces.
    // "proto" refers to the object's prototype (which implements the interface)
    // whereas "this.prototype" is the interface's prototype (which may still
    // contain unimplemented methods.)

    let unfulfilledReqs = this.prototype.__requires__.filter((required) => {
        // Either the interface is not present or it is not listed before the
        // interface that requires it or the class does not inherit it. This is
        // so that required interfaces don't copy over properties from other
        // interfaces that require them.
        let interfaces = proto.__interfaces__;
        return ((!_interfacePresent(required, proto) ||
            interfaces.indexOf(required) > interfaces.indexOf(this)) &&
            !(proto instanceof required));
    }).map((required) =>
        // __name__ is only present on GJS-created classes and will be the most
        // accurate name. required.name will be present on introspected GObjects
        // but is not preferred because it will be the C name. The last option
        // is just so that we print something if there is garbage in Requires.
        required.prototype.__name__ || required.name || required);
    if (unfulfilledReqs.length > 0) {
        throw new Error('The following interfaces must be implemented before ' +
            this.prototype.__name__ + ': ' + unfulfilledReqs.join(', '));
    }

    // Check that this interface's required methods are implemented
    let unimplementedFns = Object.getOwnPropertyNames(this.prototype)
    .filter((p) => this.prototype[p] === Interface.UNIMPLEMENTED)
    .filter((p) => !(p in proto) || proto[p] === Interface.UNIMPLEMENTED);
    if (unimplementedFns.length > 0)
        throw new Error('The following members of ' + this.prototype.__name__ +
            ' are not implemented yet: ' + unimplementedFns.join(', '));
};

Interface.prototype.toString = function () {
    return '[interface ' + this.__name__ + ' for ' + this.prototype.__name__ + ']';
};

Interface.prototype._init = function (params) {
    let name = params.Name;

    let propertyObj = {};
    Object.getOwnPropertyNames(params)
    .filter((name) => ['Name', 'Requires'].indexOf(name) === -1)
    .forEach((name) => {
        let descriptor = Object.getOwnPropertyDescriptor(params, name);

        // Create wrappers on the interface object so that generics work (e.g.
        // SomeInterface.some_function(this, blah) instead of
        // SomeInterface.prototype.some_function.call(this, blah)
        if (typeof descriptor.value === 'function') {
            let interfaceProto = this.prototype;  // capture in closure
            this[name] = function () {
                return interfaceProto[name].call.apply(interfaceProto[name],
                    arguments);
            };
        }

        // writable and enumerable are inherited, see note in Class._init()
        descriptor.configurable = false;

        propertyObj[name] = descriptor;
    });

    Object.defineProperties(this.prototype, propertyObj);
    Object.defineProperties(this.prototype, {
        '__name__': { writable: false,
                      configurable: false,
                      enumerable: false,
                      value: name },
        '__requires__': { writable: false,
                          configurable: false,
                          enumerable: false,
                          value: params.Requires || [] }
    });
};
