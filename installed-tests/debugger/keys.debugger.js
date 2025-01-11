// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>
const a = {
    foo: 1,
    bar: null,
    tres: undefined,
    [Symbol('s')]: 'string',
};
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
const b = new PrivateTest();
debugger;
void (a, b);
