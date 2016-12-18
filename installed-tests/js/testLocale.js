describe('JS_SetLocaleCallbacks', function () {
    // Requesting the weekday name tests locale_to_unicode
    it('toLocaleDateString() works', function () {
        let date = new Date('12/15/1981');
        let datestr = date.toLocaleDateString('pt-BR', { weekday: 'long' });
        expect(datestr).toEqual('terça-feira');
    });

    it('toLocaleLowerCase() works', function () {
        expect('AAA'.toLocaleLowerCase()).toEqual('aaa');
    });

    // String conversion is implemented internally to GLib,
    // and is more-or-less independent of locale. (A few
    // characters are handled specially for a few locales,
    // like i in Turkish. But not A WITH ACUTE)
    it('toLocaleLowerCase() works for Unicode', function () {
        expect('\u00c1'.toLocaleLowerCase()).toEqual('\u00e1');
    });

    it('toLocaleUpperCase() works', function () {
        expect('aaa'.toLocaleUpperCase()).toEqual('AAA');
    });

    it('toLocaleUpperCase() works for Unicode', function () {
        expect('\u00e1'.toLocaleUpperCase()).toEqual('\u00c1');
    });

    // GLib calls out to libc for collation, so we can't really
    // assume anything - we could even be running in the
    // C locale. The below is pretty safe.
    it('localeCompare() works', function () {
        expect('a'.localeCompare('b')).toBeLessThan(0);
        expect('a'.localeCompare('a')).toEqual(0);
        expect('b'.localeCompare('a')).toBeGreaterThan(0);
    });
});
