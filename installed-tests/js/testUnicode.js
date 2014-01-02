const JSUnit = imports.jsUnit;

function testUnicode() {
    JSUnit.assertEquals(6, 'Погода'.length);
    JSUnit.assertEquals(1055, 'Погода'.charCodeAt(0));
    JSUnit.assertEquals(1086, 'Погода'.charCodeAt(3));
    JSUnit.assertEquals("\u65e5", "日本語".charAt(0));
    JSUnit.assertEquals("\u672c", "日本語".charAt(1));
    JSUnit.assertEquals("\u8a9e", "日本語".charAt(2));
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

