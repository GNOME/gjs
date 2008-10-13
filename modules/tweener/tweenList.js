/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC. All Rights Reserved. */
/**
 * The tween list object. Stores all of the properties and information that pertain to individual tweens.
 *
 * @author              Nate Chatellier, Zeh Fernando
 * @version             1.0.4
 * @private
 */
/*
Licensed under the MIT License

Copyright (c) 2006-2007 Zeh Fernando and Nate Chatellier

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

http://code.google.com/p/tweener/
http://code.google.com/p/tweener/wiki/License
*/
log("Loading tweenlist.js");

function TweenList(scope, timeStart, timeComplete,
                      useFrames, transition, transitionParams) {
    this._init(scope, timeStart, timeComplete, useFrames, transition,
               transitionParams);
}

TweenList.prototype = {
    _init: function(scope, timeStart, timeComplete,
                    userFrames, transition, transitionParams) {
        this.scope = scope;
        this.timeStart = timeStart;
        this.timeComplete = timeComplete;
        this.userFrames = userFrames;
        this.transition = transition;
        this.transitionParams = transitionParams;

        /* Other default information */
        this.properties = new Object();
        this.isPaused = false;
        this.timePaused = undefined;
        this.isCaller = false;
        this.updatesSkipped = 0;
        this.timesCalled = 0;
        this.skipUpdates = 0;
        this.hasStarted = false;
    },

    clone: function(omitEvents) {
        var tween = new TweenList(scope, timeStart, timeComplete, useFrames,
                                  transition, transitionParams);
        tween.properties = new Array();
        for (let name in properties) {
            tween.properties[name] = properties[name].clone();
        }
        tween.skipUpdates = skipUpdates;
        tween.updatesSkipped = updatesSkipped;

        if (!omitEvents) {
            tween.onStart = onStart;
            tween.onUpdate = onUpdate;
            tween.onComplete = onComplete;
            tween.onOverwrite = onOverwrite;
            tween.onError = onError;
            tween.onStartParams = onStartParams;
            tween.onUpdateParams = onUpdateParams;
            tween.onCompleteParams = onCompleteParams;
            tween.onOverwriteParams = onOverwriteParams;
            tween.onStartScope = onStartScope;
            tween.onUpdateScope = onUpdateScope;
            tween.onCompleteScope = onCompleteScope;
            tween.onOverwriteScope = onOverwriteScope;
            tween.onErrorScope = onErrorScope;
        }
        tween.rounded = rounded;
        tween.isPaused = isPaused;
        tween.timePaused = timePaused;
        tween.isCaller = isCaller;
        tween.count = count;
        tween.timesCalled = timesCalled;
        tween.waitFrames = waitFrames;
        tween.hasStarted = hasStarted;

        return tween;
    }
};

function makePropertiesChain(obj) {
    /* Tweener has a bunch of code here to get all the properties of all
     * the objects we inherit from (the objects in the 'base' property).
     * I don't think that applies to JavaScript...
     */
    return obj;
};

log("Done loading tweenlist.js");
