// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

/** @deprecated Use GLib.idle_add instead. */
export declare function idle_add(handler: Function, priority?: number): number;
/** @deprecated Use GLib.MainLoop.quit instead. */
export declare function quit(id: string): void;
/** @deprecated Use GLib.MainLoop.run instead. */
export declare function run(id: string): void;
/** @deprecated Use GLib.Source.remove instead. */
export declare function source_remove(id: number): void;
/** @deprecated Use GLib.timeout_add instead. */
export declare function timeout_add(
    timeout_ms: number,
    handler: Function,
    priority?: number,
): number;
/** @deprecated Use GLib.timeout_add_seconds instead. */
export declare function timeout_add_seconds(
    timeout_s: number,
    handler: Function,
    priority?: number,
): number;
