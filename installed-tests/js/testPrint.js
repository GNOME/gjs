describe('print', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'print');
        print('foo');
        expect(print).toHaveBeenCalledWith('foo');
    });
});

describe('printerr', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'printerr');
        printerr('foo');
        expect(printerr).toHaveBeenCalledWith('foo');
    });
});

describe('log', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'log');
        log('foo');
        expect(log).toHaveBeenCalledWith('foo');
    });
});

describe('logError', function () {
    it('can be spied upon', function () {
        spyOn(globalThis, 'logError');
        logError('foo', 'bar');
        expect(logError).toHaveBeenCalledWith('foo', 'bar');
    });
});
