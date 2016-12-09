// tests for JS_SetLocaleCallbacks().
const JSUnit = imports.jsUnit;

function testToLocaleDateString() {
    let date = new Date('12/15/1981');
    // Requesting the weekday name tests locale_to_unicode
    let datestr = date.toLocaleDateString('pt-BR', { weekday: 'long' });
    JSUnit.assertEquals('terça-feira', datestr);
}

function testToLocaleLowerCase() {
    JSUnit.assertEquals("aaa", "AAA".toLocaleLowerCase());

    // String conversion is implemented internally to GLib,
    // and is more-or-less independent of locale. (A few
    // characters are handled specially for a few locales,
    // like i in Turkish. But not A WITH ACUTE)
    JSUnit.assertEquals("\u00e1", "\u00c1".toLocaleLowerCase());
}

function testToLocaleUpperCase() {
    JSUnit.assertEquals("AAA", "aaa".toLocaleUpperCase());
    JSUnit.assertEquals("\u00c1", "\u00e1".toLocaleUpperCase());
}

function testToLocaleCompare() {
    // GLib calls out to libc for collation, so we can't really
    // assume anything - we could even be running in the
    // C locale. The below is pretty safe.
    JSUnit.assertTrue("a".localeCompare("b") < 0);
    JSUnit.assertEquals( 0, "a".localeCompare("a"));
    JSUnit.assertTrue("b".localeCompare("a") > 0);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

