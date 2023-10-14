/* -*- mode: js; indent-tabs-mode: nil; -*- */
/* exported bind, copyProperties, copyPublicProperties, countProperties, Class,
getMetaClass, Interface */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

// Utilities that are "meta-language" things like manipulating object props

var {Class, Interface, getMetaClass} = imports._legacy;

function countProperties(obj) {
    let count = 0;
    for (let unusedProperty in obj)
        count += 1;
    return count;
}

function getPropertyDescriptor(obj, property) {
    if (Object.hasOwn(obj, property))
        return Object.getOwnPropertyDescriptor(obj, property);
    return getPropertyDescriptor(Object.getPrototypeOf(obj), property);
}

function _copyProperty(source, dest, property) {
    let descriptor = getPropertyDescriptor(source, property);
    Object.defineProperty(dest, property, descriptor);
}

function copyProperties(source, dest) {
    for (let property in source)
        _copyProperty(source, dest, property);
}

function copyPublicProperties(source, dest) {
    for (let property in source) {
        if (typeof property === 'string' && property.startsWith('_'))
            continue;
        else
            _copyProperty(source, dest, property);
    }
}

/**
 * Binds obj to callback. Makes it possible to refer to "obj"
 * using this within the callback.
 *
 * @param {object} obj the object to bind
 * @param {Function} callback callback to bind obj in
 * @param {*} bindArguments additional arguments to the callback
 * @returns {Function} a new callback
 */
function bind(obj, callback, ...bindArguments) {
    if (typeof obj !== 'object') {
        throw new Error(`first argument to Lang.bind() must be an object, not ${
            typeof obj}`);
    }

    if (typeof callback !== 'function') {
        throw new Error(`second argument to Lang.bind() must be a function, not ${
            typeof callback}`);
    }

    // Use ES5 Function.prototype.bind, but only if not passing any bindArguments,
    // because ES5 has them at the beginning, not at the end
    if (arguments.length === 2)
        return callback.bind(obj);

    let me = obj;
    return function (...args) {
        args = args.concat(bindArguments);
        return callback.apply(me, args);
    };
}
