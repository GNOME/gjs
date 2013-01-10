// application/javascript;version=1.8
// tests for JS_SetLocaleCallbacks().

function testToLocaleDateString() {
    let date = new Date();
    // %A is the weekday name, this tests locale_to_unicode
    // we're basically just testing for a non-crash, since
    // we'd have to run in a specific locale to have any
    // idea about the result.
    date.toLocaleDateString("%A");
}

function testToLocaleLowerCase() {
    assertEquals("aaa", "AAA".toLocaleLowerCase());

    // String conversion is implemented internally to GLib,
    // and is more-or-less independent of locale. (A few
    // characters are handled specially for a few locales,
    // like i in Turkish. But not A WITH ACUTE)
    assertEquals("\u00e1", "\u00c1".toLocaleLowerCase());

    // Unpaired surrogate, can't be converted to UTF-8
    assertRaises(function() { "\ud800".toLocaleLowerCase(); });
}

function testToLocaleUpperCase() {
    assertEquals("AAA", "aaa".toLocaleUpperCase());
    assertEquals("\u00c1", "\u00e1".toLocaleUpperCase());
    assertRaises(function() { "\ud800".toLocaleUpperCase(); });
}

function testToLocaleCompare() {
    // GLib calls out to libc for collation, so we can't really
    // assume anything - we could even be running in the
    // C locale. The below is pretty safe.
    assertEquals(-1, "a".localeCompare("b"));
    assertEquals( 0, "a".localeCompare("a"));
    assertEquals( 1, "b".localeCompare("a"));

    // Again test error handling when conversion fails
    //assertRaises(function() { "\ud800".localeCompare("a"); });
    //assertRaises(function() { "a".localeCompare("\ud800"); });
}

gjstestRun();
