/* -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil; -*- */
/* eslint-disable no-unused-vars */
// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: 2008 litl, LLC.
// SPDX-FileCopyrightText: 2001 Robert Penner

/**
 * Equations
 * Main equations for the Tweener class
 *
 * @author              Zeh Fernando, Nate Chatellier
 * @version             1.0.2
 */

/* exported easeInBack, easeInBounce, easeInCirc, easeInCubic, easeInElastic,
easeInExpo, easeInOutBack, easeInOutBounce, easeInOutCirc, easeInOutCubic,
easeInOutElastic, easeInOutExpo, easeInOutQuad, easeInOutQuart, easeInOutQuint,
easeInOutSine, easeInQuad, easeInQuart, easeInQuint, easeInSine, easeNone,
easeOutBack, easeOutBounce, easeOutCirc, easeOutCubic, easeOutElastic,
easeOutExpo, easeOutInBack, easeOutInBounce, easeOutInCirc, easeOutInCubic,
easeOutInElastic, easeOutInExpo, easeOutInQuad, easeOutInQuart, easeOutInQuint,
easeOutInSine, easeOutQuad, easeOutQuart, easeOutQuint, easeOutSine, linear */

// ==================================================================================================================================
// TWEENING EQUATIONS functions -----------------------------------------------------------------------------------------------------
// (the original equations are Robert Penner's work as mentioned on the disclaimer)

/**
 * Easing equation function for a simple linear tweening, with no easing.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeNone(t, b, c, d, pParams) {
    return c * t / d + b;
}

/* Useful alias */
function linear(t, b, c, d, pParams) {
    return easeNone(t, b, c, d, pParams);
}

/**
 * Easing equation function for a quadratic (t^2) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInQuad(t, b, c, d, pParams) {
    return c * (t /= d) * t + b;
}

/**
 * Easing equation function for a quadratic (t^2) easing out: decelerating to zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutQuad(t, b, c, d, pParams) {
    return -c * (t /= d) * (t - 2) + b;
}

/**
 * Easing equation function for a quadratic (t^2) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutQuad(t, b, c, d, pParams) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t + b;
    return -c / 2 * (--t * (t - 2) - 1) + b;
}

/**
 * Easing equation function for a quadratic (t^2) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInQuad(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutQuad(t * 2, b, c / 2, d, pParams);
    return easeInQuad(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a cubic (t^3) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInCubic(t, b, c, d, pParams) {
    return c * (t /= d) * t * t + b;
}

/**
 * Easing equation function for a cubic (t^3) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutCubic(t, b, c, d, pParams) {
    return c * ((t = t / d - 1) * t * t + 1) + b;
}

/**
 * Easing equation function for a cubic (t^3) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutCubic(t, b, c, d, pParams) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t * t + b;
    return c / 2 * ((t -= 2) * t * t + 2) + b;
}

/**
 * Easing equation function for a cubic (t^3) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInCubic(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutCubic(t * 2, b, c / 2, d, pParams);
    return easeInCubic(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a quartic (t^4) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInQuart(t, b, c, d, pParams) {
    return c * (t /= d) * t * t * t + b;
}

/**
 * Easing equation function for a quartic (t^4) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutQuart(t, b, c, d, pParams) {
    return -c * ((t = t / d - 1) * t * t * t - 1) + b;
}

/**
 * Easing equation function for a quartic (t^4) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutQuart(t, b, c, d, pParams) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t * t * t + b;
    return -c / 2 * ((t -= 2) * t * t * t - 2) + b;
}

/**
 * Easing equation function for a quartic (t^4) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInQuart(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutQuart(t * 2, b, c / 2, d, pParams);
    return easeInQuart(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a quintic (t^5) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInQuint(t, b, c, d, pParams) {
    return c * (t /= d) * t * t * t * t + b;
}

/**
 * Easing equation function for a quintic (t^5) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutQuint(t, b, c, d, pParams) {
    return c * ((t = t / d - 1) * t * t * t * t + 1) + b;
}

/**
 * Easing equation function for a quintic (t^5) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutQuint(t, b, c, d, pParams) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t * t * t * t + b;
    return c / 2 * ((t -= 2) * t * t * t * t + 2) + b;
}

/**
 * Easing equation function for a quintic (t^5) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInQuint(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutQuint(t * 2, b, c / 2, d, pParams);
    return easeInQuint(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a sinusoidal (sin(t)) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInSine(t, b, c, d, pParams) {
    return -c * Math.cos(t / d * (Math.PI / 2)) + c + b;
}

/**
 * Easing equation function for a sinusoidal (sin(t)) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutSine(t, b, c, d, pParams) {
    return c * Math.sin(t / d * (Math.PI / 2)) + b;
}

/**
 * Easing equation function for a sinusoidal (sin(t)) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutSine(t, b, c, d, pParams) {
    return -c / 2 * (Math.cos(Math.PI * t / d) - 1) + b;
}

/**
 * Easing equation function for a sinusoidal (sin(t)) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInSine(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutSine(t * 2, b, c / 2, d, pParams);
    return easeInSine(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for an exponential (2^t) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInExpo(t, b, c, d, pParams) {
    return t <= 0 ? b : c * Math.pow(2, 10 * (t / d - 1)) + b;
}

/**
 * Easing equation function for an exponential (2^t) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutExpo(t, b, c, d, pParams) {
    return t >= d ? b + c : c * (-Math.pow(2, -10 * t / d) + 1) + b;
}

/**
 * Easing equation function for an exponential (2^t) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutExpo(t, b, c, d, pParams) {
    if (t <= 0)
        return b;
    if (t >= d)
        return b + c;
    if ((t /= d / 2) < 1)
        return c / 2 * Math.pow(2, 10 * (t - 1)) + b;
    return c / 2 * (-Math.pow(2, -10 * --t) + 2) + b;
}

/**
 * Easing equation function for an exponential (2^t) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInExpo(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutExpo(t * 2, b, c / 2, d, pParams);
    return easeInExpo(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a circular (sqrt(1-t^2)) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInCirc(t, b, c, d, pParams) {
    return -c * (Math.sqrt(1 - (t /= d) * t) - 1) + b;
}

/**
 * Easing equation function for a circular (sqrt(1-t^2)) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutCirc(t, b, c, d, pParams) {
    return c * Math.sqrt(1 - (t = t / d - 1) * t) + b;
}

/**
 * Easing equation function for a circular (sqrt(1-t^2)) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutCirc(t, b, c, d, pParams) {
    if ((t /= d / 2) < 1)
        return -c / 2 * (Math.sqrt(1 - t * t) - 1) + b;
    return c / 2 * (Math.sqrt(1 - (t -= 2) * t) + 1) + b;
}

/**
 * Easing equation function for a circular (sqrt(1-t^2)) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInCirc(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutCirc(t * 2, b, c / 2, d, pParams);
    return easeInCirc(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for an elastic (exponentially decaying sine wave) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param a             Amplitude.
 * @param p             Period.
 * @return              The correct value.
 */
function easeInElastic(t, b, c, d, pParams) {
    if (t <= 0)
        return b;
    if ((t /= d) >= 1)
        return b + c;
    var p = !pParams || isNaN(pParams.period) ? d * .3 : pParams.period;
    var s;
    var a = !pParams || isNaN(pParams.amplitude) ? 0 : pParams.amplitude;
    if (!a || a < Math.abs(c)) {
        a = c;
        s = p / 4;
    } else {
        s = p / (2 * Math.PI) * Math.asin(c / a);
    }
    return -(a * Math.pow(2, 10 * (t -= 1)) * Math.sin((t * d - s) * (2 * Math.PI) / p)) + b;
}

/**
 * Easing equation function for an elastic (exponentially decaying sine wave) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param a             Amplitude.
 * @param p             Period.
 * @return              The correct value.
 */
function easeOutElastic(t, b, c, d, pParams) {
    if (t <= 0)
        return b;
    if ((t /= d) >= 1)
        return b + c;
    var p = !pParams || isNaN(pParams.period) ? d * .3 : pParams.period;
    var s;
    var a = !pParams || isNaN(pParams.amplitude) ? 0 : pParams.amplitude;
    if (!a || a < Math.abs(c)) {
        a = c;
        s = p / 4;
    } else {
        s = p / (2 * Math.PI) * Math.asin(c / a);
    }
    return a * Math.pow(2, -10 * t) * Math.sin((t * d - s) * (2 * Math.PI) / p) + c + b;
}

/**
 * Easing equation function for an elastic (exponentially decaying sine wave) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param a             Amplitude.
 * @param p             Period.
 * @return              The correct value.
 */
function easeInOutElastic(t, b, c, d, pParams) {
    if (t <= 0)
        return b;
    if ((t /= d / 2) >= 2)
        return b + c;
    var p = !pParams || isNaN(pParams.period) ? d * (.3 * 1.5) : pParams.period;
    var s;
    var a = !pParams || isNaN(pParams.amplitude) ? 0 : pParams.amplitude;
    if (!a || a < Math.abs(c)) {
        a = c;
        s = p / 4;
    } else {
        s = p / (2 * Math.PI) * Math.asin(c / a);
    }
    if (t < 1)
        return -.5 * (a * Math.pow(2, 10 * (t -= 1)) * Math.sin((t * d - s) * (2 * Math.PI) / p)) + b;
    return a * Math.pow(2, -10 * (t -= 1)) * Math.sin((t * d - s) * (2 * Math.PI) / p) * .5 + c + b;
}

/**
 * Easing equation function for an elastic (exponentially decaying sine wave) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param a             Amplitude.
 * @param p             Period.
 * @return              The correct value.
 */
function easeOutInElastic(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutElastic(t * 2, b, c / 2, d, pParams);
    return easeInElastic(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a back (overshooting cubic easing: (s+1)*t^3 - s*t^2) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param s             Overshoot ammount: higher s means greater overshoot (0 produces cubic easing with no overshoot, and the default value of 1.70158 produces an overshoot of 10 percent).
 * @return              The correct value.
 */
function easeInBack(t, b, c, d, pParams) {
    var s = !pParams || isNaN(pParams.overshoot) ? 1.70158 : pParams.overshoot;
    return c * (t /= d) * t * ((s + 1) * t - s) + b;
}

/**
 * Easing equation function for a back (overshooting cubic easing: (s+1)*t^3 - s*t^2) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param s             Overshoot ammount: higher s means greater overshoot (0 produces cubic easing with no overshoot, and the default value of 1.70158 produces an overshoot of 10 percent).
 * @return              The correct value.
 */
function easeOutBack(t, b, c, d, pParams) {
    var s = !pParams || isNaN(pParams.overshoot) ? 1.70158 : pParams.overshoot;
    return c * ((t = t / d - 1) * t * ((s + 1) * t + s) + 1) + b;
}

/**
 * Easing equation function for a back (overshooting cubic easing: (s+1)*t^3 - s*t^2) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param s             Overshoot ammount: higher s means greater overshoot (0 produces cubic easing with no overshoot, and the default value of 1.70158 produces an overshoot of 10 percent).
 * @return              The correct value.
 */
function easeInOutBack(t, b, c, d, pParams) {
    var s = !pParams || isNaN(pParams.overshoot) ? 1.70158 : pParams.overshoot;
    if ((t /= d / 2) < 1)
        return c / 2 * (t * t * (((s *= (1.525)) + 1) * t - s)) + b;
    return c / 2 * ((t -= 2) * t * (((s *= (1.525)) + 1) * t + s) + 2) + b;
}

/**
 * Easing equation function for a back (overshooting cubic easing: (s+1)*t^3 - s*t^2) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @param s             Overshoot ammount: higher s means greater overshoot (0 produces cubic easing with no overshoot, and the default value of 1.70158 produces an overshoot of 10 percent).
 * @return              The correct value.
 */
function easeOutInBack(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutBack(t * 2, b, c / 2, d, pParams);
    return easeInBack(t * 2 - d, b + c / 2, c / 2, d, pParams);
}

/**
 * Easing equation function for a bounce (exponentially decaying parabolic bounce) easing in: accelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInBounce(t, b, c, d, pParams) {
    return c - easeOutBounce(d - t, 0, c, d) + b;
}

/**
 * Easing equation function for a bounce (exponentially decaying parabolic bounce) easing out: decelerating from zero velocity.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutBounce(t, b, c, d, pParams) {
    if ((t /= d) < 1 / 2.75)
        return c * (7.5625 * t * t) + b;
    else if (t < 2 / 2.75)
        return c * (7.5625 * (t -= (1.5 / 2.75)) * t + .75) + b;
    else if (t < 2.5 / 2.75)
        return c * (7.5625 * (t -= (2.25 / 2.75)) * t + .9375) + b;
    else
        return c * (7.5625 * (t -= (2.625 / 2.75)) * t + .984375) + b;
}

/**
 * Easing equation function for a bounce (exponentially decaying parabolic bounce) easing in/out: acceleration until halfway, then deceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeInOutBounce(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeInBounce(t * 2, 0, c, d) * .5 + b;
    else
        return easeOutBounce(t * 2 - d, 0, c, d) * .5 + c * .5 + b;
}

/**
 * Easing equation function for a bounce (exponentially decaying parabolic bounce) easing out/in: deceleration until halfway, then acceleration.
 *
 * @param t             Current time (in frames or seconds).
 * @param b             Starting value.
 * @param c             Change needed in value.
 * @param d             Expected easing duration (in frames or seconds).
 * @return              The correct value.
 */
function easeOutInBounce(t, b, c, d, pParams) {
    if (t < d / 2)
        return easeOutBounce(t * 2, b, c / 2, d, pParams);
    return easeInBounce(t * 2 - d, b + c / 2, c / 2, d, pParams);
}
