/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC. */
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
        var tween = new TweenList(this.scope, this.timeStart, this.timeComplete, this.userFrames,
                                  this.transition, this.transitionParams);
        tween.properties = new Array();
        for (let name in this.properties) {
            tween.properties[name] = this.properties[name];
        }
        tween.skipUpdates = this.skipUpdates;
        tween.updatesSkipped = this.updatesSkipped;

        if (!omitEvents) {
            tween.onStart = this.onStart;
            tween.onUpdate = this.onUpdate;
            tween.onComplete = this.onComplete;
            tween.onOverwrite = this.onOverwrite;
            tween.onError = this.onError;
            tween.onStartParams = this.onStartParams;
            tween.onUpdateParams = this.onUpdateParams;
            tween.onCompleteParams = this.onCompleteParams;
            tween.onOverwriteParams = this.onOverwriteParams;
            tween.onStartScope = this.onStartScope;
            tween.onUpdateScope = this.onUpdateScope;
            tween.onCompleteScope = this.onCompleteScope;
            tween.onOverwriteScope = this.onOverwriteScope;
            tween.onErrorScope = this.onErrorScope;
        }
        tween.rounded = this.rounded;
        tween.isPaused = this.isPaused;
        tween.timePaused = this.timePaused;
        tween.isCaller = this.isCaller;
        tween.count = this.count;
        tween.timesCalled = this.timesCalled;
        tween.waitFrames = this.waitFrames;
        tween.hasStarted = this.hasStarted;

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
