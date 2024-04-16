# Coding style

Our goal is to have all JavaScript code in GNOME follow a consistent style. In a dynamic language like
JavaScript, it is essential to be rigorous about style (and unit tests), or you rapidly end up
with a spaghetti-code mess.

## Linter

GJS includes an eslint configuration file, `.eslintrc.yml`.
There is an additional one that applies to test code in
`installed-tests/js/.eslintrc.yml`.
We recommend using this for your project, with any modifications you
need that are particular to your project.

In most editors you can set up eslint to run on your code as you type.
Or you can set it up as a git commit hook.
In any case if you contribute code to GJS, eslint will check the code in
your merge request.

The style guide for JS code in GJS is, by definition, the eslint config
file.
This file only contains conventions that the linter can't catch.

## Imports

Use CamelCase when importing modules to distinguish them from ordinary variables, e.g.

```js
const Big = imports.big;
const {GLib} = imports.gi;
```

## Variable declaration

Always use `const` or `let` when block scope is intended.
In almost all cases `const` is correct if you don't reassign the
variable, and otherwise `let`.
In general `var` is only needed for variables that you are exporting
from a module.

```js
// Iterating over an array
for (let i = 0; i < 10; ++i) {
    let foo = bar(i);
}
// Iterating over an object's properties
for (let prop in someobj) {
    ...
}
```

If you don't use `let` or `const` then the variable is added to function
scope, not the for loop block scope.
See [What's new in JavaScript 1.7][1]

A common case where this matters is when you have a closure inside a loop:
```js
for (let i = 0; i < 10; ++i) {
    GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function () {
        log(`number is: ${i}`);
    });
}
```

If you used `var` instead of `let` it would print "10" a bunch of times.

## `this` in closures

`this` will not be captured in a closure; `this` is relative to how the closure is invoked, not to
the value of this where the closure is created, because `this` is a keyword with a value passed
in at function invocation time, it is not a variable that can be captured in closures.

To solve this, use `Function.bind()`, or arrow functions, e.g.:

```js
const closure = () => { this._fnorbate(); };
// or
const closure = function() { this._fnorbate() }.bind(this);
```

A more realistic example would be connecting to a signal on a
method of a prototype:

```js
const MyPrototype = {
    _init() {
        fnorb.connect('frobate', this._onFnorbFrobate.bind(this));
    },

    _onFnorbFrobate(fnorb) {
        this._updateFnorb();
    },
};
```

## Object literal syntax

JavaScript allows equivalently:
```js
const foo = {'bar': 42};
const foo = {bar: 42};
```
and
```js
const b = foo['bar'];
const b = foo.bar;
```

If your usage of an object is like an object, then you're defining "member variables." For member variables, use the no-quotes no-brackets syntax, that is, `{bar: 42}` and `foo.bar`.

If your usage of an object is like a hash table (and thus conceptually the keys can have special chars in them), don't use quotes, but use brackets, `{bar: 42}`, `foo['bar']`.

## Variable naming

- We use javaStyle variable names, with CamelCase for type names and lowerCamelCase for variable and method names. However, when calling a C method with underscore-based names via introspection, we just keep them looking as they do in C for simplicity.
- Private variables, whether object member variables or module-scoped variables, should begin with `_`.
- True global variables should be avoided whenever possible. If you do create them, the variable name should have a namespace in it, like `BigFoo`
- When you assign a module to an alias to avoid typing `imports.foo.bar` all the time, the alias should be `const TitleCase` so `const Bar = imports.foo.bar;`
- If you need to name a variable something weird to avoid a namespace collision, add a trailing `_` (not leading, leading `_` means private).

[1]: http://developer.mozilla.org/en/docs/index.php?title=New_in_JavaScript_1.7&printable=yes#Block_scope_with_let
