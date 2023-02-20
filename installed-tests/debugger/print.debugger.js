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
const i = {some: 'plain object', that: 'has keys'};
const j = new Set([5, 6, 7]);
const k = class J {};
const l = new GObject.Object();
const m = new Error('message');
const n = {a: 1};
const o = {some: 'plain object', [Symbol('that')]: 'has symbols'};
debugger;
void (a, b, c, d, e, f, g, h, i, j, k, l, m, n, o);
