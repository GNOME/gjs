# Class framework #

Keep in mind that JavaScript does not "really" have classes in the sense of C++ or Java; you can't create new types beyond the built-in ones (Object, Array, RegExp, String). However, you can create object instances that share common properties, including methods, using the prototype mechanism.

Every JavaScript implementation invents its own syntactic sugar for doing this, and so did we, but the basics are always the same.
In ES6, JavaScript finally went ahead and standardized some particular syntactic sugar, which GJS will support in the future.

Each JavaScript object has a property `__proto__`; if you write `obj.foo` and `foo` is not in `obj`, JavaScript will look for `foo` in `__proto__`. If several objects have the same `__proto__`, then they can share methods or other state.

You can create objects with a constructor, which is a special function. Say you have:
```js
function Foo() {}
let f = new Foo();
```

For `new Foo()` JavaScript will create a new, empty object; and execute `Foo()` with the new, empty object as `this`. So the function `Foo()` sets up the new object.

`new Foo()` will also set `__proto__` on the new object to `Foo.prototype`. The property `prototype` on a constructor is used to initialize `__proto__` for objects the constructor creates. To get the right `__proto__` on objects, we need the right prototype property on the constructor.

You could think of `f = new Foo()` as:
```js
let f = {}; // create new object
f.__proto__ = Foo.prototype; // doing this by hand isn't actually allowed
Foo.call(f); // invoke Foo() with new object as "this"
```

Our syntactic sugar is `Lang.Class` which works like this:
```js
const Lang = imports.lang;

const Foo = new Lang.Class({
  Name: 'Foo',
  _init: function(arg1, arg2) {
    this._myPrivateInstanceVariable = arg1;
  },
  myMethod: function() {

  },
  myClassVariable: 42,
  myOtherClassVariable: "Hello"
}
```

This pattern means that when you do `let f = new Foo()`, `f` will be a new object, `f.__proto__` will point to `Foo.prototype` which will be the object that you passed to `new Lang.Class()`, and `Foo.prototype._init` will be called to set up the object.

> **NOTE:** Again, on the JavaScript language level, Foo is not a class in the sense of Java or C++; it's just a constructor function, which means it's intended for use with the `new Foo()` syntax to create an object. Once the object is created, from a JavaScript language perspective its type is the built-in type `Object` - though we're using it and thinking of it as if it had type `Foo`, JavaScript doesn't have a clue about that and will never do any type-checking based on which constructor was used to create something. All typing is "duck typing." The built-in types, such as `Object`, `String`, `Error`, `RegExp`, and `Array`, _are_ real types, however, and do get type-checked.

> **NOTE:** If a constructor function has a return value, it is used as the value of `new Foo()` - replacing the automatically-created `this` object passed in to the constructor. If a constructor function returns nothing (undefined), then the passed-in `this` is used. In general, avoid this feature - constructors should have no return value. But this feature may be necessary if you need the new instance to have a built-in type other than Object. If you return a value from the constructor, `this` is simply discarded, so referring to `this` while in the constructor won't make sense.

## JavaScript "inheritance" ##

There are lots of ways to simulate "inheritance" in JavaScript. In general, it's a good idea to avoid class hierarchies in JavaScript. But sometimes it's the best solution.

Our preferred approach is to use our syntactic sugar `Lang.Class` which includes an `Extends` property which sets the prototype of the subclass to the base class. Looking up a property in the subclass starts with the properties of the instance. If the property isn't there, then the prototype chain is followed first to the subclass's prototype and then to the base class's prototype.
```js
const Lang = imports.lang;

const Base = new Lang.Class({
  Name: 'Base',
  _init : function(foo) {
    this._foo = foo;
  },
  frobate : function() {
  }
});

const Sub = new Lang.Class({
  Name: 'Sub',
  Extends: Base,
  _init: function(foo, bar) {
    // here is an example of how to "chain up"
    this.parent(foo);
    this._bar = bar;
  }

  // add a newMethod property in Sub.prototype
  newMethod : function() {
  }
});
```

> **NOTE:** You cannot use this mechanism to inherit from a built-in type, such as String or Error, because the methods on those objects expect them to have primitive type String or Error, while your constructor will create an Object instead.

In portable JavaScript code you'll often see a different technique used to get this prototype chain:
```js
function Base(foo) ...

Base.prototype = ...

function Sub(foo, bar) ...

// Do NOT do this - do not use an instance of Base as the Sub prototype
Sub.prototype = new Base();
```

The problem with this pattern is that you might want to have side effects in the `Base()` constructor. Say `Base()` in its constructor creates a window on the screen, because `Base()` is a dialog class or something. If you use the pattern that some instances of `Base()` are just prototypes for subclasses, you'll get extra windows on the screen.

The other problem with this pattern is that it's just confusing and weird.
