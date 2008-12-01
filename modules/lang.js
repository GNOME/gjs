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

function countProperties(obj) {
    let count = 0;
    for (let property in obj) {
        count += 1;
    }
    return count;
}

function _copyProperty(source, dest, property) {
    let getterFunc = source.__lookupGetter__(property);
    let setterFunc = source.__lookupSetter__(property);

    if (getterFunc) {
        dest.__defineGetter__(property, getterFunc);
    }

    if (setterFunc) {
        dest.__defineSetter__(property, setterFunc);
    }

    if (!setterFunc && !getterFunc) {
        dest[property] = source[property];
    }
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

function copyPropertiesNoOverwrite(source, dest) {
    for (let property in source) {
        if (!(property in dest)) {
            _copyProperty(source, dest, property);
        }
    }
}

function removeNullProperties(obj) {
    for (let property in obj) {
        if (obj[property] == null)
            delete obj[property];
        else if (typeof(obj[property]) == 'object')
            removeNullProperties(obj[property]);
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
    let me = obj;
    let bindArguments = Array.prototype.slice.call(arguments, 2);

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

    return function() {
        let args = Array.prototype.slice.call(arguments);
        args = args.concat(bindArguments);
        return callback.apply(me, args);
    };
}
