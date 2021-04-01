// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

/* exported setTimeout, setInterval, clearTimeout, clearInterval */

const {GLib} = imports.gi;

const jobs = imports._promiseNative;

// It should not be possible to remove or destroy sources from outside this library.
const ids = new Map();
const releasedIds = [];
let idIncrementor = 1;

/**
 * @param {number} sourceId the source ID to generate a timer ID for
 * @returns {number}
 */
function nextId(sourceId) {
    let id;

    if (releasedIds.length > 0) {
        id = releasedIds.pop();
    } else {
        idIncrementor++;

        id = idIncrementor;
    }

    ids.set(id, sourceId);
    return id;
}

function releaseId(sourceId) {
    ids.delete(sourceId);
    releasedIds.push(sourceId);
}

const TIMEOUT_MAX = 2 ** 31 - 1;

function checkThis(thisArg) {
    if (thisArg !== null && thisArg !== undefined && thisArg !== globalThis)
        throw new TypeError('Illegal invocation');
}

function checkBigInt(n) {
    if (typeof n === 'bigint')
        throw new TypeError('Cannot convert a BigInt value to a number');
}

function ToNumber(interval) {
    /* eslint-disable no-implicit-coercion */
    if (typeof interval === 'number')
        return interval;
    else if (typeof interval === 'object')
        return +interval.valueOf() || +interval;


    return +interval;
    /* eslint-enable */
}

function setTimeout(callback, delay = 0, ...args) {
    checkThis(this);
    checkBigInt(delay);

    delay = wrapDelay(delay);
    const cb = callback.bind(globalThis, ...args);
    const id = nextId(GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
        if (!ids.has(id))
            return GLib.SOURCE_REMOVE;


        cb();
        releaseId(id);
        // Drain the microtask queue.
        jobs.run();


        return GLib.SOURCE_REMOVE;
    }));

    return id;
}

function wrapDelay(delay) {
    if (delay > TIMEOUT_MAX) {
        imports._print.warn(
            `${delay} does not fit into` +
            ' a 32-bit signed integer.' +
            '\nTimeout duration was set to 1.'
        );
        delay = 1;
    }
    return Math.max(0, delay | 0);
}

function setInterval(callback, delay = 0, ...args) {
    checkThis(this);
    checkBigInt(delay);

    delay = wrapDelay(delay);
    const cb = callback.bind(globalThis, ...args);
    const id = nextId(GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
        if (!ids.has(id))
            return GLib.SOURCE_REMOVE;


        cb();

        // Drain the microtask queue.
        jobs.run();

        return GLib.SOURCE_CONTINUE;
    }));

    return id;
}

function _clearTimer(id) {
    checkBigInt(id);

    const _id = ToNumber(id);

    if (!ids.has(_id))
        return;


    const cx = GLib.MainContext.default();
    const source_id = ids.get(_id);
    const source = cx.find_source_by_id(source_id);

    if (source_id > 0 && source) {
        GLib.source_remove(source_id);
        source.destroy();
        releaseId(_id);
    }
}

function clearTimeout(id = 0) {
    _clearTimer(id);
}

function clearInterval(id = 0) {
    _clearTimer(id);
}
