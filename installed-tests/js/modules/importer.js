// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

// Imports a specifier from this module, so that the import map's scopes for
// this module's URL apply
export function dynamicImport(specifier) {
    return import(specifier);
}
