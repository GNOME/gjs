// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

const system = import.meta.importSync('system');

export let {
    addressOf,
    addressOfGObject,
    breakpoint,
    clearDateCaches,
    dumpHeap,
    dumpMemoryInfo,
    exit,
    gc,
    programArgs,
    programInvocationName,
    programPath,
    refcount,
    version,
} = system;

export default {
    addressOf,
    addressOfGObject,
    breakpoint,
    clearDateCaches,
    dumpHeap,
    dumpMemoryInfo,
    exit,
    gc,
    programArgs,
    programInvocationName,
    programPath,
    refcount,
    version,
};
