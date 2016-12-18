const Regress = imports.gi.Regress;

describe('GI repository namespace', function () {
    it('supplies a name', function () {
        expect(Regress.__name__).toEqual('Regress');
    });
});
