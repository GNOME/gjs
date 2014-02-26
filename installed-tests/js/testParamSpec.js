const JSUnit = imports.jsUnit;

const Regress = imports.gi.Regress;
const GObject = imports.gi.GObject;

let name = 'foo-property';
let nick = 'Foo property';
let blurb = 'This is the foo property';
let flags = GObject.ParamFlags.READABLE;

function testStringParamSpec() {
    let stringSpec = GObject.ParamSpec.string(name, nick, blurb, flags,
                                              'Default Value');

    JSUnit.assertEquals(name, stringSpec.name);
    JSUnit.assertEquals(nick, stringSpec._nick);
    JSUnit.assertEquals(blurb, stringSpec._blurb);
    JSUnit.assertEquals(flags, stringSpec.flags);
    JSUnit.assertEquals('Default Value', stringSpec.default_value);
}

function testIntParamSpec() {
    let intSpec = GObject.ParamSpec.int(name, nick, blurb, flags,
                                        -100, 100, -42);

    JSUnit.assertEquals(name, intSpec.name);
    JSUnit.assertEquals(nick, intSpec._nick);
    JSUnit.assertEquals(blurb, intSpec._blurb);
    JSUnit.assertEquals(flags, intSpec.flags);
    JSUnit.assertEquals(-42, intSpec.default_value);
}

function testUIntParamSpec() {
    let uintSpec = GObject.ParamSpec.uint(name, nick, blurb, flags,
                                          20, 100, 42);

    JSUnit.assertEquals(name, uintSpec.name);
    JSUnit.assertEquals(nick, uintSpec._nick);
    JSUnit.assertEquals(blurb, uintSpec._blurb);
    JSUnit.assertEquals(flags, uintSpec.flags);
    JSUnit.assertEquals(42, uintSpec.default_value);
}

function testInt64ParamSpec() {
    let int64Spec = GObject.ParamSpec.int64(name, nick, blurb, flags,
                                            0x4000,
                                            0xffffffff,
                                            0x2266bbff);

    JSUnit.assertEquals(name, int64Spec.name);
    JSUnit.assertEquals(nick, int64Spec._nick);
    JSUnit.assertEquals(blurb, int64Spec._blurb);
    JSUnit.assertEquals(flags, int64Spec.flags);
    JSUnit.assertEquals(0x2266bbff, int64Spec.default_value);
}

function testUInt64ParamSpec() {
    let uint64Spec = GObject.ParamSpec.uint64(name, nick, blurb, flags,
                                              0,
                                              0xffffffff,
                                              0x2266bbff);

    JSUnit.assertEquals(name, uint64Spec.name);
    JSUnit.assertEquals(nick, uint64Spec._nick);
    JSUnit.assertEquals(blurb, uint64Spec._blurb);
    JSUnit.assertEquals(flags, uint64Spec.flags);
    JSUnit.assertEquals(0x2266bbff, uint64Spec.default_value);
}

function testEnumParamSpec() {
    let enumSpec = GObject.ParamSpec.enum(name, nick, blurb, flags,
                                          Regress.TestEnum,
                                          Regress.TestEnum.VALUE2);

    JSUnit.assertEquals(name, enumSpec.name);
    JSUnit.assertEquals(nick, enumSpec._nick);
    JSUnit.assertEquals(blurb, enumSpec._blurb);
    JSUnit.assertEquals(flags, enumSpec.flags);
    JSUnit.assertEquals(Regress.TestEnum.VALUE2, enumSpec.default_value);
}

function testFlagsParamSpec() {
    let flagsSpec = GObject.ParamSpec.flags(name, nick, blurb, flags,
                                            Regress.TestFlags,
                                            Regress.TestFlags.FLAG2);

    JSUnit.assertEquals(name, flagsSpec.name);
    JSUnit.assertEquals(nick, flagsSpec._nick);
    JSUnit.assertEquals(blurb, flagsSpec._blurb);
    JSUnit.assertEquals(flags, flagsSpec.flags);
    JSUnit.assertEquals(Regress.TestFlags.FLAG2, flagsSpec.default_value);
}

function testParamSpecMethod() {
    let objectSpec = GObject.ParamSpec.object(name, nick, blurb, flags, GObject.Object);

    JSUnit.assertEquals(name, objectSpec.get_name());
    JSUnit.assertEquals(nick, objectSpec.get_nick());
    JSUnit.assertEquals(blurb, objectSpec.get_blurb());
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

