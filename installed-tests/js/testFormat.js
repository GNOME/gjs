// tests for imports.format module

const Format = imports.format;
const JSUnit = imports.jsUnit;

function testEscape() {
    var foo = '%d%%'.format(10);
    JSUnit.assertEquals("escaped '%'", "10%", foo);
}

function testStrings() {
    var foo = '%s'.format("Foo");
    var foobar = '%s %s'.format("Foo", "Bar");
    var barfoo = '%2$s %1$s'.format("Foo", "Bar");

    JSUnit.assertEquals("single string argument", "Foo", foo);
    JSUnit.assertEquals("two string arguments", "Foo Bar", foobar);
    JSUnit.assertEquals("two swapped string arguments", "Bar Foo", barfoo);
}

function testFixedNumbers() {
    var foo = '%d'.format(42);
    var bar = '%x'.format(42);

    JSUnit.assertEquals("base-10 42", "42", foo);
    JSUnit.assertEquals("base-16 42", "2a", bar);
}

function testFloating() {
    var foo = '%f'.format(0.125);
    var bar = '%.2f'.format(0.125);

    JSUnit.assertEquals("0.125, no precision", "0.125", foo);
    JSUnit.assertEquals("0.125, precision 2", "0.13", bar);
}

function testPadding() {
    let zeroFormat = '%04d';
    var foo1 = zeroFormat.format(1);
    var foo10 = zeroFormat.format(10);
    var foo100 = zeroFormat.format(100);

    let spaceFormat = '%4d';
    var bar1 = spaceFormat.format(1);
    var bar10 = spaceFormat.format(10);
    var bar100 = spaceFormat.format(100);

    JSUnit.assertEquals("zero-padding 1", "0001", foo1);
    JSUnit.assertEquals("zero-padding 10", "0010", foo10);
    JSUnit.assertEquals("zero-padding 100", "0100", foo100);

    JSUnit.assertEquals("space-padding 1", "   1", bar1);
    JSUnit.assertEquals("space-padding 10", "  10", bar10);
    JSUnit.assertEquals("space-padding 100", " 100", bar100);
}

function testErrors() {
    JSUnit.assertRaises(function() { return '%z'.format(42); });
    JSUnit.assertRaises(function() { return '%.2d'.format(42); });
    JSUnit.assertRaises(function() { return '%Ix'.format(42); });
    JSUnit.assertRaises(function() { return '%2$d %d %1$d'.format(1, 2, 3); });
}

String.prototype.format = Format.format;
JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

