// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

const {EXIT_CODE} = await import('./dynamicTopLevelAwaitImportee.js');
const system = await import('system');

system.exit(EXIT_CODE);
