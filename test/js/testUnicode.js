// application/javascript;version=1.8

function testUnicode() {
    assertEquals(6, 'Погода'.length);
    assertEquals(1055, 'Погода'.charCodeAt(0));
    assertEquals(1086, 'Погода'.charCodeAt(3));
    assertEquals("\u65e5", "日本語".charAt(0));
    assertEquals("\u672c", "日本語".charAt(1));
    assertEquals("\u8a9e", "日本語".charAt(2));
}

gjstestRun();
