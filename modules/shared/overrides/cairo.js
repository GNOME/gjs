// This override adds the builtin Cairo bindings to imports.gi.cairo.
// (It's confusing to have two incompatible ways to import Cairo.)

/** @type {Object.<string, any>} */
var module = {};

/**
 * @param {string} ns
 */
let $import = (ns) => imports[ns];

/**
 * @param {(ns: string) => any} require
 */
function _init(require) {
    if (require) {
        $import = require;
    }

    Object.assign(this, $import('cairo'));
}

module.exports = {
    _init
}