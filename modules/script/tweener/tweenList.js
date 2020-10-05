/* -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2006-2007 Zeh Fernando and Nate Chatellier
// SPDX-FileCopyrightText: 2008 litl, LLC.

/**
 * The tween list object. Stores all of the properties and information that pertain to individual tweens.
 *
 * @author              Nate Chatellier, Zeh Fernando
 * @version             1.0.4
 * @private
 */
/* exported makePropertiesChain, TweenList */
/*
http://code.google.com/p/tweener/
http://code.google.com/p/tweener/wiki/License
*/

function TweenList(scope, timeStart, timeComplete,
    useFrames, transition, transitionParams) {
    this._init(scope, timeStart, timeComplete, useFrames, transition,
        transitionParams);
}

TweenList.prototype = {
    _init(scope, timeStart, timeComplete,
        userFrames, transition, transitionParams) {
        this.scope = scope;
        this.timeStart = timeStart;
        this.timeComplete = timeComplete;
        this.userFrames = userFrames;
        this.transition = transition;
        this.transitionParams = transitionParams;

        /* Other default information */
        this.properties = {};
        this.isPaused = false;
        this.timePaused = undefined;
        this.isCaller = false;
        this.updatesSkipped = 0;
        this.timesCalled = 0;
        this.skipUpdates = 0;
        this.hasStarted = false;
    },

    clone(omitEvents) {
        var tween = new TweenList(this.scope, this.timeStart, this.timeComplete, this.userFrames,
            this.transition, this.transitionParams);
        tween.properties = [];
        for (let name in this.properties)
            tween.properties[name] = this.properties[name];
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
        tween.min = this.min;
        tween.max = this.max;
        tween.isPaused = this.isPaused;
        tween.timePaused = this.timePaused;
        tween.isCaller = this.isCaller;
        tween.count = this.count;
        tween.timesCalled = this.timesCalled;
        tween.waitFrames = this.waitFrames;
        tween.hasStarted = this.hasStarted;

        return tween;
    },
};

function makePropertiesChain(obj) {
    /* Tweener has a bunch of code here to get all the properties of all
     * the objects we inherit from (the objects in the 'base' property).
     * I don't think that applies to JavaScript...
     */
    return obj;
}
