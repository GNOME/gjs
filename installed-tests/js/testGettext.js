// -*- mode: js; indent-tabs-mode: nil -*-

const Gettext = imports.gettext;

describe('Gettext module', function () {
    // We don't actually want to mess with the locale, so just use setlocale's
    // query mode. We also don't want to make this test locale-dependent, so
    // just assert that it returns a string with at least length 1 (the shortest
    // locale is "C".)
    it('setlocale returns a locale', function () {
        let locale = Gettext.setlocale(Gettext.LocaleCategory.ALL, null);
        expect(locale.length).not.toBeLessThan(1);
    });
});
