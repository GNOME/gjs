// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

const Mainloop = imports.mainloop;

describe('Mainloop.timeout_add()', function () {
    let runTenTimes, runOnlyOnce, neverRun, neverRunSource;
    beforeAll(function (done) {
        let count = 0;
        runTenTimes = jasmine.createSpy('runTenTimes').and.callFake(() => {
            if (count === 10) {
                done();
                return false;
            }
            count += 1;
            return true;
        });
        runOnlyOnce = jasmine.createSpy('runOnlyOnce').and.returnValue(false);
        neverRun = jasmine.createSpy('neverRun').and.throwError();

        Mainloop.timeout_add(10, runTenTimes);
        Mainloop.timeout_add(10, runOnlyOnce);
        neverRunSource = Mainloop.timeout_add(90000, neverRun);
    });

    it('runs a timeout function', function () {
        expect(runOnlyOnce).toHaveBeenCalledTimes(1);
    });

    it('runs a timeout function until it returns false', function () {
        expect(runTenTimes).toHaveBeenCalledTimes(11);
    });

    it('runs a timeout function after an initial timeout', function () {
        expect(neverRun).not.toHaveBeenCalled();
    });

    afterAll(function () {
        Mainloop.source_remove(neverRunSource);
    });
});

describe('Mainloop.idle_add()', function () {
    let runOnce, runTwice, neverRuns, quitAfterManyRuns;
    beforeAll(function (done) {
        runOnce = jasmine.createSpy('runOnce').and.returnValue(false);
        runTwice = jasmine.createSpy('runTwice').and.returnValues([true, false]);
        neverRuns = jasmine.createSpy('neverRuns').and.throwError();
        let count = 0;
        quitAfterManyRuns = jasmine.createSpy('quitAfterManyRuns').and.callFake(() => {
            count += 1;
            if (count > 10) {
                done();
                return false;
            }
            return true;
        });

        Mainloop.idle_add(runOnce);
        Mainloop.idle_add(runTwice);
        let neverRunsId = Mainloop.idle_add(neverRuns);
        Mainloop.idle_add(quitAfterManyRuns);

        Mainloop.source_remove(neverRunsId);
    });

    it('runs an idle function', function () {
        expect(runOnce).toHaveBeenCalledTimes(1);
    });

    it('continues to run idle functions that return true', function () {
        expect(runTwice).toHaveBeenCalledTimes(2);
        expect(quitAfterManyRuns).toHaveBeenCalledTimes(11);
    });

    it('does not run idle functions if removed', function () {
        expect(neverRuns).not.toHaveBeenCalled();
    });

    it('can remove idle functions while they are being invoked', function (done) {
        let removeId = Mainloop.idle_add(() => {
            Mainloop.source_remove(removeId);
            done();
            return false;
        });
    });

    // Add an idle before exit, then never run main loop again.
    // This is to test that we remove idle callbacks when the associated
    // JSContext is blown away. The leak check in minijasmine will
    // fail if the idle function is not garbage collected.
    it('does not leak idle callbacks', function () {
        Mainloop.idle_add(() => {
            fail('This should never have been called');
            return true;
        });
    });
});
