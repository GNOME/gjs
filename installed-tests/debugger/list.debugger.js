// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Mantoh Nasah Kuma <nasahnash20@gmail.com>
function divide(a, b) {
    if (b === 0)
        return undefined;
    else if (a === undefined || b === undefined)
        return undefined;
    else
        return a / b;
}
divide();
