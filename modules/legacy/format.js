// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

/** @type {Object.<string, any>} */
var module = {};

const { vprintf } = imports._format;

/**
 * @param {any[]} args
 */
function printf(...args) {
    let fmt = args.shift();
    print(vprintf(fmt, args));
}

/*
 * This function is intended to extend the String object and provide
 * an String.format API for string formatting.
 * It has to be set up using String.prototype.format = Format.format;
 * Usage:
 * "somestring %s %d".format('hello', 5);
 * It supports %s, %d, %x and %f, for %f it also support precisions like
 * "%.2f".format(1.526). All specifiers can be prefixed with a minimum
 * field width, e.g. "%5s".format("foo"). Unless the width is prefixed
 * with '0', the formatted string will be padded with spaces.
 */
/**
 * @param {any[]} args
 */
function format(...args) {
    return vprintf(this, args);
}

module.exports = { format, printf };
