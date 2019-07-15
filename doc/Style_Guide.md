# Coding style #

Our goal is to have all JavaScript code in GNOME follow a consistent style. In a dynamic language like
JavaScript, it is essential to be rigorous about style (and unit tests), or you rapidly end up
with a spaghetti-code mess.

## Semicolons ##

JavaScript allows omitting semicolons at the end of lines, but don't. Always end
statements with a semicolon.

## js2-mode ##

If using Emacs, try js2-mode. It functions as a "lint" by highlighting missing semicolons
and the like.

## Imports ##

Use CamelCase when importing modules to distinguish them from ordinary variables, e.g.

```js
const Big = imports.big;
const {GLib} = imports.gi;
```

## Variable declaration ##

Always use one of `const`, `var`, or `let` when defining a variable. Always use `let` when block scope is intended; in particular, inside `for()` and `while()` loops, `let` is almost always correct.

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

If you don't use `let` then the variable is added to function scope, not the for loop block scope.
See [What's new in JavaScript 1.7][1]

A common case where this matters is when you have a closure inside a loop:
```js
for (let i = 0; i < 10; ++i) {
    GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() {
        log(`number is: ${i}`);
    });
}
```

If you used `var` instead of `let` it would print "10" a bunch of times.

Inside functions, `let` is always correct instead of `var` as far as we know. `var` is useful when you want to add something to the `with()` object, though... in particular we think you need `var` to define module variables, since our module system loads modules with the equivalent of `with (moduleObject)`

## `this` in closures ##

`this` will not be captured in a closure; `this` is relative to how the closure is invoked, not to
the value of this where the closure is created, because `this` is a keyword with a value passed
in at function invocation time, it is not a variable that can be captured in closures.

To solve this, use `Function.bind()`, or fat arrow functions, e.g.:

```js
let closure = function() { this._fnorbate() }.bind(this);
// or
let closure = () => { this._fnorbate(); };
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

## Object literal syntax ##

JavaScript allows equivalently:
```js
foo = {'bar': 42};
foo = {bar: 42};
```
and
```js
var b = foo['bar'];
var b = foo.bar;
```

If your usage of an object is like an object, then you're defining "member variables." For member variables, use the no-quotes no-brackets syntax, that is, `{bar: 42}` and `foo.bar`.

If your usage of an object is like a hash table (and thus conceptually the keys can have special chars in them), don't use quotes, but use brackets, `{bar: 42}`, `foo['bar']`.

## Variable naming ##

- We use javaStyle variable names, with CamelCase for type names and lowerCamelCase for variable and method names. However, when calling a C method with underscore-based names via introspection, we just keep them looking as they do in C for simplicity.
- Private variables, whether object member variables or module-scoped variables, should begin with `_`.
- True global variables (in the global or 'window' object) should be avoided whenever possible. If you do create them, the variable name should have a namespace in it, like `BigFoo`
- When you assign a module to an alias to avoid typing `imports.foo.bar` all the time, the alias should be `const TitleCase` so `const Bar = imports.foo.bar;`
- If you need to name a variable something weird to avoid a namespace collision, add a trailing `_` (not leading, leading `_` means private).
- For GObject constructors, always use the `lowerCamelCase` style for property names instead of dashes or underscores.

## Whitespace ##

* 4-space indentation (the Java style)
* No trailing whitespace.
* No tabs.
* If you `chmod +x .git/hooks/pre-commit` it will not let you commit with messed-up whitespace (well, it doesn't catch tabs. turn off tabs in your text editor.)

## JavaScript attributes ##

Don't use the getter/setter syntax when getting and setting has side effects, that is, the code:
```js
foo.bar = 10;
```
should not do anything other than save "10" as a property of `foo`. It's obfuscated otherwise; if the setting has side effects, it's better if it looks like a method.

In practice this means the only use of attributes is to create read-only properties:
```js
get bar() {
    return this._bar;
}
```

If the property requires a setter, or if getting it has side effects, methods are probably clearer.

[1] http://developer.mozilla.org/en/docs/index.php?title=New_in_JavaScript_1.7&printable=yes#Block_scope_with_let
