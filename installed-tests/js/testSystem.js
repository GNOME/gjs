const System = imports.system;

describe('System.addressOf()', function () {
    it('gives the same result for the same object', function () {
        let o = {};
        expect(System.addressOf(o)).toEqual(System.addressOf(o));
    });

    it('gives different results for different objects', function () {
        expect(System.addressOf({})).not.toEqual(System.addressOf({}));
    });
});

describe('System.version', function () {
    it('gives a plausible number', function () {
        expect(System.version).not.toBeLessThan(13600);
    });
});
