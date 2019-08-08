const {GObject, Regress} = imports.gi;

describe('Fundamental type support', function () {
    it('can marshal a subtype of a custom fundamental type into a GValue', function () {
        const fund = new Regress.TestFundamentalSubObject('plop');
        expect(() => GObject.strdup_value_contents(fund)).not.toThrow();
    });
});
