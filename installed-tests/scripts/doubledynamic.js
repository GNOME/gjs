// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// this JS script should succeed without an error on the second import
let done = false;

import('./doubledynamicImportee.js')
    .then(ddi => {
        ddi.noop();
    })
    .finally(() => {
        if (done)
            imports.mainloop.quit();
        done = true;
    });

import('./doubledynamicImportee.js')
    .then(ddi => {
        ddi.noop();
    })
    .finally(() => {
        if (done)
            imports.mainloop.quit();
        done = true;
    });

imports.mainloop.run();
