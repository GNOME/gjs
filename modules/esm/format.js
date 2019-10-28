// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

/* exported format, printf, vprintf */
import { vprintf } from "_format"

export { vprintf } from "_format"

/**
 * @param {string} str
 * @param  {...any} args 
 * @returns {void}
 */
export function printf(str, ...args) {
    let fargs = args.slice();
    console.log(vprintf(str, fargs));
}