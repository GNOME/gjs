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

