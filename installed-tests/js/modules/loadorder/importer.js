// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

// importMap.json remaps this URL for modules in loadorder/
export {default} from 'resource:///org/gjs/jsunit/modules/sub/x.js';

// Dynamic imports from here are also inside the loadorder/ scope
export function dynamicImport(specifier) {
    return import(specifier);
}
