// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

const system = import.meta.importSync('system');

export let {
    addressOf,
    breakpoint,
    clearDateCaches,
    exit,
    gc,
    programInvocationName,
    programPath,
    refcount,
    version,
} = system;

export default {
    addressOf,
    breakpoint,
    clearDateCaches,
    exit,
    gc,
    programInvocationName,
    programPath,
    refcount,
    version,
};
