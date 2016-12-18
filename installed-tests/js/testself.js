describe('Test harness internal consistency', function () {
    it('', function () {
        var someUndefined;
        var someNumber = 1;
        var someOtherNumber = 42;
        var someString = "hello";
        var someOtherString = "world";

        expect(true).toBeTruthy();
        expect(false).toBeFalsy();

        expect(someNumber).toEqual(someNumber);
        expect(someString).toEqual(someString);

        expect(someNumber).not.toEqual(someOtherNumber);
        expect(someString).not.toEqual(someOtherString);

        expect(null).toBeNull();
        expect(someNumber).not.toBeNull();
        expect(someNumber).toBeDefined();
        expect(someUndefined).not.toBeDefined();
        expect(0 / 0).toBeNaN();
        expect(someNumber).not.toBeNaN();

        expect(() => { throw {}; }).toThrow();

        expect(() => expect(true).toThrow()).toThrow();
        expect(() => true).not.toThrow();
    });
});
