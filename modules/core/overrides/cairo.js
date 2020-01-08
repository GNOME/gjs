// This override adds the builtin Cairo bindings to imports.gi.cairo.
// (It's confusing to have two incompatible ways to import Cairo.)

function _init() {
    Object.assign(this, imports.cairo);
}
