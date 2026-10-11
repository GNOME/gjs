// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import('./dynamicImplicitMainloopImportee.js')
    .then(({EXIT_CODE}) => {
        imports.system.exit(EXIT_CODE);
    });
