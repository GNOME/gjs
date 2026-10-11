// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

try {
    await import(ARGV[0]);
} catch {
    imports.system.exit(1);
}
