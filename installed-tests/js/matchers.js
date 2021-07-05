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
            return `${JSON.stringify(elements)}`;
        },
    };
}
