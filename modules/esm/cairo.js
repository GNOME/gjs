// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

const cairo = import.meta.importSync('cairoNative');

export default Object.assign(
    {},
    imports._cairo,
    cairo
);
