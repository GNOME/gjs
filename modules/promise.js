/* Copyright (c) 2009-2010 litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/* Promises represent a value which will be computed in the future,
 * although it may not be available yet.
 *
 * An "async calls" convention is then built on promises, in
 * async.js. It is generally possible to write code using only the
 * Async API without explicitly using Promise.  It's also possible
 * (but often more typing) to write code with explicit Promise
 * manipulation. One or the other may be nicer in a given instance.
 *
 * Convention: when an API returns a promise, that promise should be
 * "kicked off" already (the thread or main loop source should be
 * started). It should not be necessary to invoke get() on the promise
 * to get the computation going.
 *
 * Convention: it is OK for Promise.get() to invoke the onReturn or
 * onError synchronously (_before_ the get() returns). This can
 * occasionally be surprising, but the alternatives are not
 * good either and the synchronous result can be useful.
 */

/** default onError handler, makes debugging missing callbacks easier.
 * It is a bug if this handler gets called.
 */
const DEFAULT_ONERROR = function(e) {
    logError(e, "No onError handler");
};

/* This is publicly exported from async.js, but is defined here to
 * avoid a circular dependency.
 *
 * The issue is that we want async.js to build on Promise, but as
 * a nice tweak want Promise.get to be an async call as defined
 * in async.js
 */
let _asyncFunc = function(f) {
    /* Create a task from an Asynchronous Function, providing 'this' as
     * the first argument. */
    f.asyncCall = function() {
        let params = Array.slice(arguments);
        let me = params[0];
        params[0] = f;
        return _asyncCall.apply(me, params);
    };
    return f;
};

/* This is publicly exported from async.js, but is defined here to
 * avoid a circular dependency.
 *
 * _asyncCall is needed by _asyncFunc above)
 */
let _asyncCall = function(f) {
    let params = Array.slice(arguments, 1); // take off 'f'
    let promise = new Promise();
    let onReturn = function(v) { promise.putReturn(v); };
    let onError  = function(e) { promise.putError(e); };
    params.unshift(onReturn, onError);
    f.apply(this /* preserve 'this' */, params);
    return promise;
};

/** Prototype for a Promise.
 *
 *  A promise is a value (or error) that may not be available yet.
 *  To obtain this value, we may need to return to the main loop
 *  or run a main loop recursively.
 *
 * @constructor
 */
function Promise() { this._queue = []; }

Promise.prototype = {
    get waiting() {
        return this.hasOwnProperty('_queue');
    },

    /** Invoke 'onReturn' on the result of this promise, when
     * it completes.  Any error will invoke 'onError' with the thrown
     * exception.  (The get method of a Promise is itself
     * an Asynchronous Function, see async.js)
     */
    get : _asyncFunc(function(onReturn, onError) {
        if (!this.waiting)
            throw new Error("get after get (should be cached!)");
        onError = onError || DEFAULT_ONERROR; // catch bugs
        /* no value available yet, queue continuations. */
        this._queue.push({ onReturn: onReturn, onError: onError });
    }),

    /** Set a normal value of a promise (only callable once), invoking any
     * queued onReturn continuations as necessary.
     */
    putReturn : function(returnValue) {
        if (!this.waiting)
            throw new Error("putReturn after put");
        // mark this promise as no longer 'waiting'
        let queue = this._queue;
        delete this._queue;
        // prevent further queuing
        this.get = _asyncFunc(function(onReturn, onError) {
            onReturn(returnValue);
        });
        // okay, now invoke queued callbacks.
        for each (let cb in queue) {
            try {
                cb.onReturn(returnValue);
            } catch (e) {
                logError(e, "Error in onReturn callback");
                // but make sure all other callbacks are still invoked.
            }
        }
    },
    /** Set an error value of a promise (only callable once), invoking any
     * queued onError continuations as necessary.
     */
    putError : function(errorValue) {
        if (!this.waiting)
            throw new Error("putError after put");
        // mark this promise as no longer 'waiting'
        let queue = this._queue;
        delete this._queue;
        // prevent further queuing
        this.get = _asyncFunc(function(onReturn, onError) {
            onError(errorValue);
        });
        // okay, now invoke queued callbacks.
        for each (let cb in queue) {
            try {
                cb.onError(errorValue);
            } catch (e) {
                logError(e, "Error in onError callback");
                // but make sure all other callbacks are still invoked.
            }
        }
    },

    /** Utility method: fire off the promise, don't wait for a result (but
     * log any error which occurs).
     */
    fireAndForget: function() {
        this.get(function(){}, DEFAULT_ONERROR);
    },

    /** Utility method: sets our return (or error) to the result of
     * another promise. Allows easily chaining promises.  If you
     * putPromisedReturn(undefined) (or with no args) then it is
     * equivalent to putReturn(undefined), it immediately completes
     * the promise but with no result value.
     */
    putPromisedReturn : function(promiseOfReturn) {
        let promise = this;
        if (promiseOfReturn !== undefined) {
            promiseOfReturn.get(function(v) {
                                    promise.putReturn(v);
                                },
                                function(e) {
                                    promise.putError(e);
                                });
        } else {
            promise.putReturn();
        }
    },

    toString: function() {
        return "[Promise]";
    }
};

/** Create a Promise representing the future construction of an object using
 * the given constructor (first arg) and arguments array.  The value of the
 * promise is the fully-constructed object.
 *
 * The constructor must be a special "async constructor" which has
 * onComplete and onError functions as the first two args.
 *
 * @returns a new promise, with newly-constructed object as expected value
 */
let fromConstructor = function(constructor, params) {
    params = params || [];
    let newobj = {
        __proto__: constructor.prototype,
        constructor: constructor
    };

    let promise = new Promise();
    // return the constructed object when the constructor completes
    let onComplete = function() { promise.putReturn(newobj); };
    let onError  = function(e) { promise.putError(e); };
    params.unshift(onComplete, onError);

    constructor.apply(newobj, params);
    return promise;
};

/** Converts a synchronous function into a promise. This is mostly
 * useful for testing, since the promise is never actually deferred,
 * of course. In fact the function gets called immediately.
 * We don't do anything with threads or the main loop here.
 *
 *  @returns a new promise, with value set to result of invoking function
 */
let fromSync = function(f, params) {
    params = params || [];
    let promise = new Promise();
    try {
        let v = f.apply(this, params);
        promise.putReturn(v);
    } catch (e) {
        promise.putError(e);
    }
    return promise;
};

/** Converts a value to an already-completed promise.
 * This is useful when you have a synchronous result
 * already available and want to return it through an
 * abstract API that returns a Promise.
 *
 * @returns a new promise with the given value already set
 */
let fromValue = function(v) {
    let promise = new Promise();
    promise.putReturn(v);
    return promise;
};

let _oneGeneratorStep = function(g, retval, isException, generatorResultPromise) {
    try {
        /* get the next asynchronous task to execute from the generator */
        let promise = (isException) ? g.throw(retval) : g.send(retval);
        /* execute it, with a continuation which will send the result
         * back to the generator (whether normal or exception) and
         * loop (with a tail call). */
        promise.get(function(v) {
                        _oneGeneratorStep(g, v, false, generatorResultPromise);
                    }, function(e) {
                        _oneGeneratorStep(g, e, true, generatorResultPromise);
                    });
    } catch (e) {
        /* before handling the exception, close the generator */
        try {
            g.close();
        } catch (ee) {
            /* same semantics as javascript: exception thrown in
             * finally clause overrides any other exception or
             * return value. */
            generatorResultPromise.putError(ee);
            return;
        }
        if (e === StopIteration) {
            /* generator exited without returning a value. */
            generatorResultPromise.putReturn(); /* done */
        } else if ('_asyncRetval' in e) {
            /* generator exited returning a value. */
            generatorResultPromise.putReturn(e._asyncRetval);
        } else {
            /* generator threw an exception explicitly. */
            generatorResultPromise.putError(e);
        }
    }
};

/** Converts a generator function (with "this" and array of params)
 * into a promise that promises the error or return value of the
 * generator function.
 *
 * A generator function should contain statements of the form:
 *
 * try {
 *   let promiseResult = yield promise;
 * } catch (promiseError) {
 * }
 *
 * At each such yield, the fromGenerator() driver will take
 * back control, let the promise complete, then pass back
 * control giving the result of the promise (or throwing the error
 * from the promise).
 *
 * The power of this is that the generator function can perform a series
 * of async actions, blocking on each promise in turn, while still having
 * a nice apparent flow of control. In other words this is a great way
 * to implement a chain of async steps, where each depends on the previous.
 *
 * The generator function can return a value by calling
 * Promise.putReturnFromGenerator(), which is implemented somewhat hackily
 * by throwing a special kind of exception.  The result is that the
 * generator ends, and you can Promise.get() the returned value from
 * the promise that fromGenerator() gave you. If the generator just
 * falls off the end without calling Promise.putReturnFromGenerator(), the
 * promise will have an undefined value.
 *
 * @returns a new promise whose eventual value depends on the generator
 */

let fromGenerator = function(g, self, params) {
    params = params || [];
    let generatorResultPromise = new Promise();

    try {
        let generator = g.apply(self, params);
        _oneGeneratorStep(generator, undefined, false, generatorResultPromise);
    } catch (e) {
        /* catch exceptions invoking g() to create generator. */
        generatorResultPromise.putError(e);
    }

    return generatorResultPromise;
};

/** Calls putReturn() on the promise returned by fromGenerator(),
 * ending that generator and completing its promise.
 */
let putReturnFromGenerator = function(val) {
    throw { _asyncRetval: val };
};
