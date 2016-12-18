describe('GI importer', function () {
    it('can import GI modules', function () {
        var GLib = imports.gi.GLib;
        expect(GLib.MAJOR_VERSION).toEqual(2);
    });
});

describe('Importer', function () {
    let oldSearchPath;
    let foobar, subA, subB, subFoobar;

    beforeAll(function () {
        oldSearchPath = imports.searchPath.slice();
        imports.searchPath = ['resource:///org/gjs/jsunit/modules'];

        foobar = imports.foobar;
        subA = imports.subA;
        subB = imports.subA.subB;
        subFoobar = subB.foobar;
    });

    afterAll(function () {
        imports.searchPath = oldSearchPath;
    });

    it('exists', function () {
        expect(imports).toBeDefined();
    });

    it('throws an error when trying to import a nonexistent module', function () {
        expect(() => imports.nonexistentModuleName).toThrow();
    });

    it('throws an error when evaluating the module file throws an error', function () {
        expect(() => imports.alwaysThrows).toThrow();
        // Try again to make sure that we properly discarded the module object
        expect(() => imports.alwaysThrows).toThrow();
    });

    it('can import a module', function () {
        expect(foobar).toBeDefined();
        expect(foobar.foo).toEqual('This is foo');
        expect(foobar.bar).toEqual('This is bar');
    });

    it('makes deleting the import a no-op', function () {
        expect(delete imports.foobar).toBeFalsy();
        expect(imports.foobar).toBe(foobar);
    });

    it('gives the same object when importing a second time', function () {
        foobar.somethingElse = 'Should remain';
        const foobar2 = imports.foobar;
        expect(foobar2.somethingElse).toEqual('Should remain');
    });

    it('can import a submodule', function () {
        expect(subB).toBeDefined();
        expect(subFoobar).toBeDefined();
        expect(subFoobar.foo).toEqual('This is foo');
        expect(subFoobar.bar).toEqual('This is bar');
    });

    it('does not share the same object for a module on a different path', function () {
        foobar.somethingElse = 'Should remain';
        expect(subFoobar.somethingElse).not.toBeDefined();
    });

    it('gives the same object when importing a submodule a second time', function () {
        subFoobar.someProp = 'Should be here';
        const subFoobar2 = imports.subA.subB.foobar;
        expect(subFoobar2.someProp).toEqual('Should be here');
    });

    it('has no meta properties on the toplevel importer', function () {
        expect(imports.__moduleName__).toBeNull();
        expect(imports.__parentModule__).toBeNull();
    });

    it('sets the names of imported modules', function () {
        expect(subA.__moduleName__).toEqual('subA');
        expect(subB.__moduleName__).toEqual('subB');
    });

    it('gives a module the importer object as parent module', function () {
        expect(subA.__parentModule__).toBe(imports);
    });

    it('gives a submodule the module as parent module', function () {
        expect(subB.__parentModule__).toBe(subA);
    });

    // We want to check that the copy of the 'a' module imported directly
    // is the same as the copy that 'b' imports, and that we don't have two
    // copies because of the A imports B imports A loop.
    it('does not make a separate copy of a module imported in two places', function () {
        let A = imports.mutualImport.a;
        A.incrementCount();
        expect(A.getCount()).toEqual(1);
        expect(A.getCountViaB()).toEqual(1);
    });

    it('evaluates an __init__.js file in an imported directory', function () {
        expect(subB.testImporterFunction()).toEqual('__init__ function tested');
    });

    it('accesses a class defined in an __init__.js file', function () {
        let o = new subB.ImporterClass();
        expect(o).not.toBeNull();
        expect(o.testMethod()).toEqual('__init__ class tested');
    });

    it('can import a file encoded in UTF-8', function () {
        const ModUnicode = imports.modunicode;
        expect(ModUnicode.uval).toEqual('const \u2665 utf8');
    });
});
