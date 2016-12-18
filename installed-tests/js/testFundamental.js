const Regress = imports.gi.Regress;

describe('Fundamental type support', function () {
    it('constructs a subtype of a fundamental type', function () {
        expect(() => new Regress.TestFundamentalSubObject('plop')).not.toThrow();
    });
});
