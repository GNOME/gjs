const JSUnit = imports.jsUnit;

function testImporterGI() {
    var GLib = imports.gi.GLib;
    JSUnit.assertEquals(GLib.MAJOR_VERSION, 2);
}

function testImporter() {
    JSUnit.assertNotUndefined(imports);

    JSUnit.assertRaises(() => imports.nonexistentModuleName);
    JSUnit.assertRaises(() => imports.alwaysThrows);

    // Try again to make sure that we properly discarded the module object
    JSUnit.assertRaises(() => imports.alwaysThrows);

    // Import a non-broken module
    const foobar = imports.foobar;
    JSUnit.assertNotUndefined(foobar);
    JSUnit.assertNotUndefined(foobar.foo);
    JSUnit.assertNotUndefined(foobar.bar);
    JSUnit.assertEquals(foobar.foo, "This is foo");
    JSUnit.assertEquals(foobar.bar, "This is bar");

    // Check that deleting the import is a no-op (imported properties are
    // permanent)
    JSUnit.assertFalse(delete imports.foobar);
    JSUnit.assert(imports.foobar == foobar);

    // check that importing a second time gets the same object
    foobar.somethingElse = "Should remain";
    const foobar2 = imports.foobar;
    JSUnit.assertNotUndefined(foobar2.somethingElse);
    JSUnit.assertEquals(foobar2.somethingElse, "Should remain");

    // Try a sub-module
    const subB = imports.subA.subB;
    JSUnit.assertNotUndefined(subB);
    const subFoobar = subB.foobar;
    JSUnit.assertNotUndefined(subFoobar);
    JSUnit.assertNotUndefined(subFoobar.foo);
    JSUnit.assertNotUndefined(subFoobar.bar);
    JSUnit.assertEquals(subFoobar.foo, "This is foo");
    JSUnit.assertEquals(subFoobar.bar, "This is bar");
    // subFoobar should not be the same as foobar, even though
    // they have the same basename.
    JSUnit.assertUndefined(subFoobar.somethingElse);
    // importing subFoobar a second time should get the same module
    subFoobar.someProp = "Should be here";
    const subFoobar2 = imports.subA.subB.foobar;
    JSUnit.assertNotUndefined(subFoobar2.someProp);
    JSUnit.assertEquals(subFoobar2.someProp, "Should be here");
}

function testImporterMetaProps() {
    const subA = imports.subA;
    const subB = imports.subA.subB;

    JSUnit.assertNull('imports module name', imports.__moduleName__);
    JSUnit.assertNull('imports has no parent', imports.__parentModule__);
    JSUnit.assertEquals('imports.subA module name', 'subA', subA.__moduleName__);
    JSUnit.assertEquals('imports.subA parent module', imports, subA.__parentModule__);
    JSUnit.assertEquals('imports.subA.subB module name', 'subB', subB.__moduleName__);
    JSUnit.assertEquals('imports.subA.subB parent module', subA, subB.__parentModule__);
}

function testMutualImport() {
    // We want to check that the copy of the 'a' module imported directly
    // is the same as the copy that 'b' imports, and that we don't have two
    // copies because of the A imports B imports A loop.

    let A = imports.mutualImport.a;
    A.incrementCount();
    JSUnit.assertEquals(1, A.getCount());
    JSUnit.assertEquals(1, A.getCountViaB());
}

function testImporterFunctionFromInitFile() {
    const subB = imports.subA.subB;

    JSUnit.assertNotUndefined(subB.testImporterFunction);

    let result = subB.testImporterFunction();

    JSUnit.assertEquals(result, "__init__ function tested");
}

function testImporterClassFromInitFile() {
    const subB = imports.subA.subB;

    JSUnit.assertNotUndefined(subB.ImporterClass);

    let o = new subB.ImporterClass();

    JSUnit.assertNotNull(o);

    let result = o.testMethod();

    JSUnit.assertEquals(result, "__init__ class tested");
}

function testImporterUTF8() {
    const ModUnicode = imports.modunicode;
    JSUnit.assertEquals(ModUnicode.uval, "const \u2665 utf8");
}

let oldSearchPath;

function setUp() {
    JSUnit.setUp();
    oldSearchPath = imports.searchPath.slice();
    imports.searchPath = ['resource:///org/gjs/jsunit/modules'];
}

function tearDown() {
    JSUnit.tearDown();
    imports.searchPath = oldSearchPath;
}

JSUnit.gjstestRun(this, setUp, tearDown);
