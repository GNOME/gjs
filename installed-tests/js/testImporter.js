// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

describe('GI importer', function () {
    it('can import GI modules', function () {
        var GLib = imports.gi.GLib;
        expect(GLib.MAJOR_VERSION).toEqual(2);
    });

    describe('on failure', function () {
        // For these tests, we provide special overrides files to sabotage the
        // import, at the path resource:///org/gjs/jsunit/modules/badOverrides.
        let oldSearchPath;
        beforeAll(function () {
            oldSearchPath = imports.overrides.searchPath.slice();
            imports.overrides.searchPath = ['resource:///org/gjs/jsunit/modules/badOverrides'];
        });

        afterAll(function () {
            imports.overrides.searchPath = oldSearchPath;
        });

        it("throws an exception when the overrides file can't be imported", function () {
            expect(() => imports.gi.WarnLib).toThrowError(SyntaxError);
        });

        it('throws an exception when the overrides import throws one', function () {
            expect(() => imports.gi.GIMarshallingTests).toThrow('💩');
        });

        it('throws an exception when the overrides _init throws one', function () {
            expect(() => imports.gi.Regress).toThrow('💩');
        });

        it('throws an exception when the overrides _init is a primitive', function () {
            expect(() => imports.gi.Gio).toThrowError(/_init/);
        });
    });
});

// Jasmine v3 often uses duck-typing (checking for a property to determine a type) to pretty print objects.
// Unfortunately, checking for jasmineToString and other properties causes our importer objects to throw when resolving.
// Luckily, we can override the default behavior with a custom formatter.
function formatImporter(obj) {
    if (typeof obj === 'object' && obj.toString && (obj.toString()?.startsWith('[object GjsModule') || obj.toString()?.startsWith('[GjsFileImporter ')))
        return obj.toString();

    return undefined;
}

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

    beforeEach(function () {
        jasmine.addCustomObjectFormatter(formatImporter);
    });

    it('is on the global object (backwards compatibility)', function () {
        expect(imports instanceof globalThis.GjsFileImporter).toBeTruthy();
    });

    it('is abstract', function () {
        expect(() => new globalThis.GjsFileImporter()).toThrow();
    });

    it('exists', function () {
        expect(imports).toBeDefined();
    });

    it('has a toString representation', function () {
        expect(imports.toString()).toEqual('[GjsFileImporter root]');
        expect(subA.toString()).toEqual('[GjsFileImporter subA]');
    });

    it('throws an import error when trying to import a nonexistent module', function () {
        expect(() => imports.nonexistentModuleName)
            .toThrow(jasmine.objectContaining({name: 'ImportError'}));
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

    it('can import a module with a toString property', function () {
        expect(foobar.testToString('foo')).toEqual('foo');
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

    it('imports modules with a toString representation', function () {
        expect(Object.prototype.toString.call(foobar))
            .toEqual('[object GjsModule foobar]');
        expect(subFoobar.toString())
            .toEqual('[object GjsModule subA.subB.foobar]');
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

    it('throws on an __init__.js file with a syntax error', function () {
        expect(() => imports.subBadInit.SOMETHING).toThrowError(SyntaxError);
    });

    it('throws when an __init__.js throws an error', function () {
        expect(() => imports.subErrorInit.SOMETHING).toThrowError('a bad init!');
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

    describe("properties defined in the module's lexical scope", function () {
        let LexicalScope;

        beforeAll(function () {
            globalThis.expectMe = true;
            LexicalScope = imports.lexicalScope;
        });

        it('will log a compatibility warning when accessed', function () {
            const GLib = imports.gi.GLib;
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                "Some code accessed the property 'b' on the module " +
                "'lexicalScope'.*");
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                "Some code accessed the property 'c' on the module " +
                "'lexicalScope'.*");

            void LexicalScope.b;
            void LexicalScope.c;

            // g_test_assert_expected_messages() is a macro, not introspectable
            GLib.test_assert_expected_messages_internal('Gjs',
                'testImporter.js', 179, '');
        });

        it('can be accessed', function () {
            expect(LexicalScope.a).toEqual(1);
            expect(LexicalScope.b).toEqual(2);
            expect(LexicalScope.c).toEqual(3);
            expect(LexicalScope.d).toEqual(4);
        });

        it('does not leak module properties into the global scope', function () {
            expect(globalThis.d).not.toBeDefined();
        });
    });

    describe('enumerating modules', function () {
        let keys;
        beforeEach(function () {
            keys = [];
            for (let key in imports)
                keys.push(key);
        });

        it('gets all of them', function () {
            expect(keys).toContain('foobar', 'subA', 'mutualImport', 'modunicode');
        });

        it('includes modules that throw on import', function () {
            expect(keys).toContain('alwaysThrows');
        });

        it('does not include meta properties', function () {
            expect(keys).not.toContain('__parentModule__');
            expect(keys).not.toContain('__moduleName__');
            expect(keys).not.toContain('searchPath');
        });
    });

    it("doesn't crash when resolving a non-string property", function () {
        expect(imports[0]).not.toBeDefined();
        expect(imports.foobar[0]).not.toBeDefined();
    });

    it('scripts support relative dynamic imports', async function () {
        const {say} = await import('./modules/say.js');

        expect(typeof say).toBe('function');
        expect(say('hello')).toBe('<( hello )');
    });

    it('imported scripts support relative dynamic imports', async function () {
        const response = await imports.dynamic.test();

        expect(response).toBe('<( I did it! )');
    });
});
