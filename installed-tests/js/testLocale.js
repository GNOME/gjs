// tests for JS_SetLocaleCallbacks().
const JSUnit = imports.jsUnit;

function testToLocaleDateString() {
    let date = new Date();
    // %A is the weekday name, this tests locale_to_unicode
    // we're basically just testing for a non-crash, since
    // we'd have to run in a specific locale to have any
    // idea about the result.
    date.toLocaleDateString("%A");
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

    // Again test error handling when conversion fails
    //assertRaises(function() { "\ud800".localeCompare("a"); });
    //assertRaises(function() { "a".localeCompare("\ud800"); });
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

