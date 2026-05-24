// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

type PackageInitParameters = {
    name: string;
    prefix: string;
    version: string;
    libdir?: string;
};

export var datadir: string;
export var libdir: string;
export var localedir: string;
export var moduledir: string;
export var name: string;
export var pkgdatadir: string;
export var pkglibdir: string;
export var prefix: string;
export var version: string;

export declare function checkSymbol(
    lib: string,
    ver?: string,
    symbol?: string,
): boolean;
export declare function init(params: PackageInitParameters): void;
/** @deprecated Use template strings instead. */
export declare function initFormat(): void;
/** @deprecated Use `import Gettext from 'gettext'` instead. */
export declare function initGettext(): void;
export declare function initSubmodule(module: string): void;
export declare function require(libs: { [lib: string]: string; }): void;
export declare function requireSymbol(
    lib: string,
    ver?: string,
    symbol?: string,
): void;
export declare function run<T>(module: { main: (args: string[]) => T }): T;
export declare function start(params: PackageInitParameters): number | undefined;

declare global {
    var pkg: typeof import('./package.js');
}
