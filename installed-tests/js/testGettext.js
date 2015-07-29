// -*- mode: js; indent-tabs-mode: nil -*-

// Tests for the Gettext module.
const Gettext = imports.gettext;
const JSUnit = imports.jsUnit;

function testSetlocale() {
    // We don't actually want to mess with the locale, so just use setlocale's
    // query mode. We also don't want to make this test locale-dependent, so
    // just assert that it returns a string with at least length 1 (the shortest
    // locale is "C".)
    JSUnit.assert(Gettext.setlocale(Gettext.LocaleCategory.ALL, null).length >= 1);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
