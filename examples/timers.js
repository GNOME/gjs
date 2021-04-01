// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

const promise = new Promise(r => {
    let i = 100;
    while (i--)
        ;

    r();
});

setTimeout(() => {
    promise.then(() => log('no'));
});

setTimeout(() => {
    log('de');
});
