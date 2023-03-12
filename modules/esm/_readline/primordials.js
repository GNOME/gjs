// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

/**
 * @typedef {F extends ((...args: infer Args) => infer Result) ?  ((instance: I, ...args: Args) => Result) : never} UncurriedFunction
 * @template I
 * @template F
 */

/**
 * @template {Record<string, any>} T
 * @template {keyof T} K
 * @param {T} [type] the instance type for the function
 * @param {K} key the function to curry
 * @returns {UncurriedFunction<T, T[K]>}
 */
export function uncurryThis(type, key) {
    const func = type[key];
    return (instance, ...args) => func.apply(instance, args);
}
