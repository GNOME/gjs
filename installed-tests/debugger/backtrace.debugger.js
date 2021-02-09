// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
debugger;
[[1, 2, 3, 4, 5], [6, 7, 8, 9, 10]].every(array => {
    debugger;
    array.every(num => {
        debugger;
        print(num);
        return false;
    });
    return false;
});
function mistake(array) {
    let {uninitialized_} = array.shift();
}
mistake([]);
