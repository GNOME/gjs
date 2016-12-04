const GLib = imports.gi.GLib;
const JSUnit = imports.jsUnit;

function testVariantConstructor() {
    let str_variant = new GLib.Variant('s', 'mystring');
    JSUnit.assertEquals('mystring', str_variant.get_string()[0]);
    JSUnit.assertEquals('mystring', str_variant.deep_unpack());

    let str_variant_old = GLib.Variant.new('s', 'mystring');
    JSUnit.assertTrue(str_variant.equal(str_variant_old));

    let struct_variant = new GLib.Variant('(sogvau)', [
        'a string',
        '/a/object/path',
        'asig', //nature
        new GLib.Variant('s', 'variant'),
        [ 7, 3 ]
    ]);
    JSUnit.assertEquals(5, struct_variant.n_children());

    let unpacked = struct_variant.deep_unpack();
    JSUnit.assertEquals('a string', unpacked[0]);
    JSUnit.assertEquals('/a/object/path', unpacked[1]);
    JSUnit.assertEquals('asig', unpacked[2]);
    JSUnit.assertTrue(unpacked[3] instanceof GLib.Variant);
    JSUnit.assertEquals('variant', unpacked[3].deep_unpack());
    JSUnit.assertTrue(unpacked[4] instanceof Array);
    JSUnit.assertEquals(2, unpacked[4].length);

    let maybe_variant = new GLib.Variant('ms', null);
    JSUnit.assertEquals(null, maybe_variant.deep_unpack());

    maybe_variant = new GLib.Variant('ms', 'string');
    JSUnit.assertEquals('string', maybe_variant.deep_unpack());
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
