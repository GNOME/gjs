// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

const Format = imports.format; // eslint-disable-line no-restricted-properties
const output = Format.vprintf('%Id', [60]);
print(output
  .split('')
  .map(c => c.codePointAt(0).toString(16).padStart(4, '0'))
  .join(' '));
