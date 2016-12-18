const Regress = imports.gi.Regress;
const GObject = imports.gi.GObject;

let name = 'foo-property';
let nick = 'Foo property';
let blurb = 'This is the foo property';
let flags = GObject.ParamFlags.READABLE;

function testParamSpec(type, params, defaultValue) {
    describe('GObject.ParamSpec.' + type, function () {
        let paramSpec;
        beforeEach(function () {
            paramSpec = GObject.ParamSpec[type].apply(GObject.ParamSpec,
                [name, nick, blurb, flags, ...params]);
        });

        it('has the correct name strings', function () {
            expect(paramSpec.name).toEqual(name);
            expect(paramSpec._nick).toEqual(nick);
            expect(paramSpec._blurb).toEqual(blurb);
        });

        it('has the correct flags', function () {
            expect(paramSpec.flags).toEqual(flags);
        });

        it('has the correct default value', function () {
            expect(paramSpec.default_value).toEqual(defaultValue);
        });
    });
}

testParamSpec('string', ['Default Value'], 'Default Value');
testParamSpec('int', [-100, 100, -42], -42);
testParamSpec('uint', [20, 100, 42], 42);
testParamSpec('int64', [0x4000, 0xffffffff, 0x2266bbff], 0x2266bbff);
testParamSpec('uint64', [0, 0xffffffff, 0x2266bbff], 0x2266bbff);
testParamSpec('enum', [Regress.TestEnum, Regress.TestEnum.VALUE2],
    Regress.TestEnum.VALUE2);
testParamSpec('flags', [Regress.TestFlags, Regress.TestFlags.FLAG2],
    Regress.TestFlags.FLAG2);
testParamSpec('object', [GObject.Object], null);
