// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

/**
 * A jasmine asymmetric matcher which expects an array-like object
 * to contain the given element array in the same order with the
 * same length. Useful for testing typed arrays.
 *
 * @template T
 * @param {T[]} elements an array of elements to compare with
 * @returns
 */
export function arrayLikeWithExactContents(elements) {
    return {
        /**
         * @param {ArrayLike<T>} compareTo an array-like object to compare to
         * @returns {boolean}
         */
        asymmetricMatch(compareTo) {
            return (
                compareTo.length === elements.length &&
                elements.every((e, i) => e === compareTo[i])
            );
        },
        /**
         * @returns {string}
         */
        jasmineToString() {
            return `<arrayLikeWithExactContents(${
                elements.constructor.name
            }[${JSON.stringify(Array.from(elements))}]>)`;
        },
    };
}

/**
 * A jasmine asymmetric matcher which compares a given string to an
 * array-like object of bytes. The compared bytes are decoded using
 * TextDecoder and then compared using jasmine.stringMatching.
 *
 * @param {string | RegExp} text the text or regular expression to compare decoded bytes to
 * @param {string} [encoding] the encoding of elements
 * @returns
 */
export function decodedStringMatching(text, encoding = 'utf-8') {
    const matcher = jasmine.stringMatching(text);

    return {
        /**
         * @param {ArrayLike<number>} compareTo an array of bytes to decode and compare to
         * @returns {boolean}
         */
        asymmetricMatch(compareTo) {
            const decoder = new TextDecoder(encoding);
            const decoded = decoder.decode(new Uint8Array(Array.from(compareTo)));

            return matcher.asymmetricMatch(decoded, []);
        },
        /**
         * @returns {string}
         */
        jasmineToString() {
            return `<decodedStringMatching(${text})>`;
        },
    };
}
