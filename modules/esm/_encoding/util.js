// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Node.js contributors. All rights reserved.

// Modified from https://github.com/nodejs/node/blob/78680c1cbc8b0c435963bc512e826b2a6227c315/lib/internal/encoding.js

/**
 * Trims ASCII whitespace from a string.
 * `String.prototype.trim` removes non-ASCII whitespace.
 *
 * @param {string} label the label to trim
 * @returns {string}
 */
export const trimAsciiWhitespace = label => {
    let s = 0;
    let e = label.length;
    while (
        s < e &&
        (label[s] === '\u0009' ||
            label[s] === '\u000a' ||
            label[s] === '\u000c' ||
            label[s] === '\u000d' ||
            label[s] === '\u0020')
    )
        s++;

    while (
        e > s &&
        (label[e - 1] === '\u0009' ||
            label[e - 1] === '\u000a' ||
            label[e - 1] === '\u000c' ||
            label[e - 1] === '\u000d' ||
            label[e - 1] === '\u0020')
    )
        e--;

    return label.slice(s, e);
};
