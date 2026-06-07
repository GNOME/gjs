// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

/** @deprecated Use Function.bind instead. */
export declare function bind<
    T extends object,
    A extends any[],
    B extends any[],
    F extends (...args: [...B, ...A]) => any
>(obj: T, func: F, ...bindArguments: B): (...args: A) => ReturnType<F>;
/** @deprecated Use Object.assign instead. */
export declare function copyProperties(source: object, dest: object): void;
/** @deprecated Use Object.fromEntries instead. */
export declare function copyPublicProperties(source: object, dest: object): void;
/** @deprecated Use Object.keys().length instead. */
export declare function countProperties(obj: object): number;
