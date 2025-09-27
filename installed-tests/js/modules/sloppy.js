// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

/* exported setPropertyInSloppyMode */

// ES Module code is always strict mode. In order to test something occurring in
// sloppy mode from within a module, you need to import (via the legacy
// importer) a function that was defined in a non-module context.

function setPropertyInSloppyMode(object, property, value) {
    object[property] = value;
}
