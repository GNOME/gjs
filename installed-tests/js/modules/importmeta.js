// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Philip Chimento <philip.chimento@gmail.com>

if (typeof import.meta.importSync !== 'undefined')
    throw new Error('internal import meta property should not be visible in userland');

export default Object.keys(import.meta);
