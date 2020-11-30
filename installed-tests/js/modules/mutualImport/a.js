/* exported getCount, getCountViaB, incrementCount */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 Red Hat, Inc.

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

