// This override adds the builtin Cairo bindings to imports.gi.cairo.
// (It's confusing to have two incompatible ways to import Cairo.)

import $cairo from "gi://cairo"

const cairo = require('cairo');

Object.assign($cairo, cairo);

