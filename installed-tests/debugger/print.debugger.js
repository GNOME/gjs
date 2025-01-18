// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
const {GObject} = imports.gi;
const a = undefined;
const b = null;
const c = 42;
const d = 'some string';
const e = false;
const f = true;
const g = Symbol('foobar');
const h = [1, 'money', 2, 'show', {three: 'to', 'get ready': 'go cat go'}];
const i = {some: 'plain object', that: 'has keys', with: null, and: undefined};
const j = new Set([5, 6, 7]);
const k = class J {};
const l = new GObject.Object();
const m = new Error('message');
const n = {a: 1};
const o = {some: 'plain object', [Symbol('that')]: 'has symbols'};

class Parent {
    #privateField;
    constructor() {
        this.#privateField = 1;
    }
}

class Child extends Parent {
    #subPrivateField;
    meaningOfLife = 42;
    constructor() {
        super();
        this.#subPrivateField = 2;
    }
}

class PrivateTest extends Child {
    #child;
    childVisible;
    #customToStringChild;
    #circular1;
    #circular2;
    #selfRef;
    #date;
    #privateFunc;

    constructor() {
        super();
        this.#child = new Child();
        this.childVisible = new Child();
        this.#customToStringChild = new Child();
        this.#customToStringChild.toString = () => 'Custom child!';
        this.#circular2 = {};
        this.#circular1 = {n: this.#circular2};
        this.#circular2.n = this.#circular1;
        this.#selfRef = this;
        this.#date = new Date('2025-01-07T00:53:42.417Z');
        this.#privateFunc = () => 1;
    }
}
const p = new PrivateTest();
class PrivateNullishToString {
    #test;
    toString = null;
    constructor() {
        this.#test = 1;
    }
}
const q = new PrivateNullishToString();
debugger;
void (a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q);
