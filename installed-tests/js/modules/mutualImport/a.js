/* exported getCount, getCountViaB, incrementCount */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

const B = imports.mutualImport.b;

let count = 0;

function incrementCount() {
    count++;
}

function getCount() {
    return count;
}

function getCountViaB() {
    return B.getCount();
}

