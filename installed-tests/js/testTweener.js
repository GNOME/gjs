// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

const Tweener = imports.tweener.tweener;

function installFrameTicker() {
    // Set up Tweener to have a "frame pulse" that the Jasmine clock functions
    // can influence
    let ticker = {
        FRAME_RATE: 50,

        _init() {
        },

        start() {
            this._currentTime = 0;

            this._timeoutID = setInterval(() => {
                this._currentTime += 1000 / this.FRAME_RATE;
                this.emit('prepare-frame');
            }, Math.floor(1000 / this.FRAME_RATE));
        },

        stop() {
            if ('_timeoutID' in this) {
                clearInterval(this._timeoutID);
                delete this._timeoutID;
            }

            this._currentTime = 0;
        },

        getTime() {
            return this._currentTime;
        },
    };
    imports.signals.addSignalMethods(ticker);

    Tweener.setFrameTicker(ticker);
}

describe('Tweener', function () {
    beforeAll(function () {
        jasmine.clock().install();
        installFrameTicker();
    });

    afterAll(function () {
        jasmine.clock().uninstall();
    });

    let start, update, overwrite, complete;
    beforeEach(function () {
        start = jasmine.createSpy('start');
        update = jasmine.createSpy('update');
        overwrite = jasmine.createSpy('overwrite');
        complete = jasmine.createSpy('complete');
    });

    it('runs a simple tween', function () {
        var objectA = {
            x: 0,
            y: 0,
        };

        var objectB = {
            x: 0,
            y: 0,
        };

        Tweener.addTween(objectA, {x: 10, y: 10, time: 1, transition: 'linear'});
        Tweener.addTween(objectB, {x: 10, y: 10, time: 1, delay: 0.5, transition: 'linear'});

        jasmine.clock().tick(1001);

        expect(objectA.x).toEqual(10);
        expect(objectA.y).toEqual(10);
        expect(objectB.x).toEqual(5);
        expect(objectB.y).toEqual(5);
    });

    it('calls callbacks during the tween', function () {
        Tweener.addTween({}, {
            time: 0.1,
            onStart: start,
            onUpdate: update,
            onComplete: complete,
        });

        jasmine.clock().tick(101);
        expect(start).toHaveBeenCalled();
        expect(update).toHaveBeenCalled();
        expect(complete).toHaveBeenCalled();
    });

    it('can pause tweens', function () {
        var objectA = {
            foo: 0,
        };

        var objectB = {
            bar: 0,
        };

        var objectC = {
            baaz: 0,
        };

        Tweener.addTween(objectA, {foo: 100, time: 0.1});
        Tweener.addTween(objectC, {baaz: 100, time: 0.1});
        Tweener.addTween(objectB, {bar: 100, time: 0.1});

        Tweener.pauseTweens(objectA);
        // This should do nothing
        expect(Tweener.pauseTweens(objectB, 'quux')).toBeFalsy();
        /* Pause and resume should be equal to doing nothing */
        Tweener.pauseTweens(objectC, 'baaz');
        Tweener.resumeTweens(objectC, 'baaz');

        jasmine.clock().tick(101);

        expect(objectA.foo).toEqual(0);
        expect(objectB.bar).toEqual(100);
        expect(objectC.baaz).toEqual(100);
    });

    it('can remove tweens', function () {
        var object = {
            foo: 0,
            bar: 0,
            baaz: 0,
        };

        Tweener.addTween(object, {foo: 50, time: 0.1});
        Tweener.addTween(object, {bar: 50, time: 0.1});
        Tweener.addTween(object, {baaz: 50, time: 0.1});

        /* The Tween on property foo should still be run after removing the other two */
        Tweener.removeTweens(object, 'bar', 'baaz');

        jasmine.clock().tick(101);

        expect(object.foo).toEqual(50);
        expect(object.bar).toEqual(0);
        expect(object.baaz).toEqual(0);
    });

    it('overrides a tween with another one acting on the same object and property at the same time', function () {
        var objectA = {
            foo: 0,
        };

        Tweener.addTween(objectA, {foo: 100, time: 0.1});
        Tweener.addTween(objectA, {foo: 0, time: 0.1});

        jasmine.clock().tick(101);

        expect(objectA.foo).toEqual(0);
    });

    it('does not override a tween with another one acting not at the same time', function () {
        var objectB = {
            bar: 0,
        };

        /* In this case both tweens should be executed, as they don't
         * act on the object at the same time (the second one has a
         * delay equal to the running time of the first one) */
        Tweener.addTween(objectB, {bar: 100, time: 0.1});
        Tweener.addTween(objectB, {bar: 150, time: 0.1, delay: 0.1});

        jasmine.clock(0).tick(201);

        expect(objectB.bar).toEqual(150);
    });

    it('can pause and resume all tweens', function () {
        var objectA = {
            foo: 0,
        };
        var objectB = {
            bar: 0,
        };

        Tweener.addTween(objectA, {foo: 100, time: 0.1});
        Tweener.addTween(objectB, {bar: 100, time: 0.1});

        Tweener.pauseAllTweens();

        jasmine.clock().tick(10);

        Tweener.resumeAllTweens();

        jasmine.clock().tick(101);

        expect(objectA.foo).toEqual(100);
        expect(objectB.bar).toEqual(100);
    });

    it('can remove all tweens', function () {
        var objectA = {
            foo: 0,
        };
        var objectB = {
            bar: 0,
        };

        Tweener.addTween(objectA, {foo: 100, time: 0.1});
        Tweener.addTween(objectB, {bar: 100, time: 0.1});

        Tweener.removeAllTweens();

        jasmine.clock().tick(200);

        expect(objectA.foo).toEqual(0);
        expect(objectB.bar).toEqual(0);
    });

    it('runs a tween with a time of 0 immediately', function () {
        var object = {
            foo: 100,
        };

        Tweener.addTween(object, {foo: 50, time: 0, delay: 0});
        Tweener.addTween(object, {
            foo: 200,
            time: 0.1,
            onStart: () => {
                /* The immediate tween should set it to 50 before we run */
                expect(object.foo).toEqual(50);
            },
        });

        jasmine.clock().tick(101);

        expect(object.foo).toEqual(200);
    });

    it('can call a callback a certain number of times', function () {
        var object = {
            foo: 0,
        };

        Tweener.addCaller(object, {
            onUpdate: () => {
                object.foo += 1;
            },
            count: 10,
            time: 0.1,
        });

        jasmine.clock().tick(101);

        expect(object.foo).toEqual(10);
    });

    it('can count the number of tweens on an object', function () {
        var object = {
            foo: 0,
            bar: 0,
            baaz: 0,
            quux: 0,
        };

        expect(Tweener.getTweenCount(object)).toEqual(0);

        Tweener.addTween(object, {foo: 100, time: 0.1});
        expect(Tweener.getTweenCount(object)).toEqual(1);
        Tweener.addTween(object, {bar: 100, time: 0.1});
        expect(Tweener.getTweenCount(object)).toEqual(2);
        Tweener.addTween(object, {baaz: 100, time: 0.1});
        expect(Tweener.getTweenCount(object)).toEqual(3);
        Tweener.addTween(object, {quux: 100, time: 0.1});
        expect(Tweener.getTweenCount(object)).toEqual(4);

        Tweener.removeTweens(object, 'bar', 'baaz');
        expect(Tweener.getTweenCount(object)).toEqual(2);
    });

    it('can register special properties', function () {
        Tweener.registerSpecialProperty(
            'negative_x',
            function (obj) {
                return -obj.x;
            },
            function (obj, val) {
                obj.x = -val;
            }
        );

        var objectA = {
            x: 0,
            y: 0,
        };

        Tweener.addTween(objectA, {negative_x: 10, y: 10, time: 1, transition: 'linear'});

        jasmine.clock().tick(1001);

        expect(objectA.x).toEqual(-10);
        expect(objectA.y).toEqual(10);
    });

    it('can register special modifiers for properties', function () {
        Tweener.registerSpecialPropertyModifier('discrete', discreteModifier,
            discreteGet);
        function discreteModifier(props) {
            return props.map(function (prop) {
                return {name: prop, parameters: null};
            });
        }
        function discreteGet(begin, end, time) {
            return Math.floor(begin + time * (end - begin));
        }

        var objectA = {
            x: 0,
            y: 0,
            xFraction: false,
            yFraction: false,
        };

        Tweener.addTween(objectA, {
            x: 10, y: 10, time: 1,
            discrete: ['x'],
            transition: 'linear',
            onUpdate() {
                if (objectA.x !== Math.floor(objectA.x))
                    objectA.xFraction = true;
                if (objectA.y !== Math.floor(objectA.y))
                    objectA.yFraction = true;
            },
        });

        jasmine.clock().tick(1001);

        expect(objectA.x).toEqual(10);
        expect(objectA.y).toEqual(10);
        expect(objectA.xFraction).toBeFalsy();
        expect(objectA.yFraction).toBeTruthy();
    });

    it('can split properties into more than one special property', function () {
        Tweener.registerSpecialPropertySplitter(
            'xnegy',
            function (val) {
                return [{name: 'x', value: val},
                    {name: 'y', value: -val}];
            }
        );

        var objectA = {
            x: 0,
            y: 0,
        };

        Tweener.addTween(objectA, {xnegy: 10, time: 1, transition: 'linear'});

        jasmine.clock().tick(1001);

        expect(objectA.x).toEqual(10);
        expect(objectA.y).toEqual(-10);
    });

    it('calls an overwrite callback when a tween is replaced', function () {
        var object = {
            a: 0,
            b: 0,
            c: 0,
            d: 0,
        };

        var tweenA = {
            a: 10, b: 10, c: 10, d: 10, time: 0.1,
            onStart: start,
            onOverwrite: overwrite,
            onComplete: complete,
        };
        var tweenB = {
            a: 20, b: 20, c: 20, d: 20, time: 0.1,
            onStart: start,
            onOverwrite: overwrite,
            onComplete: complete,
        };

        Tweener.addTween(object, tweenA);
        Tweener.addTween(object, tweenB);

        jasmine.clock().tick(101);

        expect(start).toHaveBeenCalledTimes(1);
        expect(overwrite).toHaveBeenCalledTimes(1);
        expect(complete).toHaveBeenCalledTimes(1);
    });

    it('can still overwrite a tween after it has started', function () {
        var object = {
            a: 0,
            b: 0,
            c: 0,
            d: 0,
        };

        var tweenA = {
            a: 10, b: 10, c: 10, d: 10, time: 0.1,
            onStart: () => {
                start();
                Tweener.addTween(object, tweenB);
            },
            onOverwrite: overwrite,
            onComplete: complete,
        };
        var tweenB = {
            a: 20, b: 20, c: 20, d: 20, time: 0.1,
            onStart: start,
            onOverwrite: overwrite,
            onComplete: complete,
        };

        Tweener.addTween(object, tweenA);

        jasmine.clock().tick(121);

        expect(start).toHaveBeenCalledTimes(2);
        expect(overwrite).toHaveBeenCalledTimes(1);
        expect(complete).toHaveBeenCalledTimes(1);
    });

    it('stays within min and max values', function () {
        var objectA = {
            x: 0,
            y: 0,
        };

        var objectB = {
            x: 0,
            y: 0,
        };

        Tweener.addTween(objectA, {x: 300, y: 300, time: 1, max: 255, transition: 'linear'});
        Tweener.addTween(objectB, {x: -200, y: -200, time: 1, delay: 0.5, min: 0, transition: 'linear'});

        jasmine.clock().tick(1001);

        expect(objectA.x).toEqual(255);
        expect(objectA.y).toEqual(255);
        expect(objectB.x).toEqual(0);
        expect(objectB.y).toEqual(0);
    });
});
