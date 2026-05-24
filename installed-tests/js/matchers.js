// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

/**
 * @overload
 * @param {Uint8Array} elements
 * @returns {jasmine.AsymmetricMatcher<ArrayLike<number>>}
 */
/**
 * @template T
 * @overload
 * @param {T[]} elements
 * @returns {jasmine.AsymmetricMatcher<ArrayLike<T>>}
 */
/**
 * A jasmine asymmetric matcher which expects an array-like object
 * to contain the given element array in the same order with the
 * same length. Useful for testing typed arrays.
 *
 * @template T
 * @param {T[] | Uint8Array} elements an array of elements to compare with
 * @returns {jasmine.AsymmetricMatcher<ArrayLike<T>>}
 */
export function arrayLikeWithExactContents(elements) {
    return {
        asymmetricMatch(compareTo) {
            return (
                compareTo.length === elements.length &&
                elements.every((e, i) => e === compareTo[i])
            );
        },
        jasmineToString() {
            return `<arrayLikeWithExactContents(${
                elements.constructor.name
            }[${JSON.stringify([...elements])}]>)`;
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
 * @returns {jasmine.AsymmetricMatcher<ArrayLike<number>>}
 */
export function decodedStringMatching(text, encoding = 'utf-8') {
    const matcher = jasmine.stringMatching(text);

    return {
        asymmetricMatch(compareTo) {
            const decoder = new TextDecoder(encoding);
            const decoded = decoder.decode(new Uint8Array(Array.from(compareTo)));

            return matcher.asymmetricMatch(decoded, []);
        },
        jasmineToString() {
            return `<decodedStringMatching(${text})>`;
        },
    };
}
