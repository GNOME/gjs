// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

const {vprintf} = imports._format;

/**
 * @param {string} str the string to format (e.g. '%s is blue')
 * @param {any...} args the arguments to replace placeholders with
 */
export function sprintf(str, ...args) {
    return vprintf(str, args);
}

export let _ = {
    sprintf,
};

export default _;
