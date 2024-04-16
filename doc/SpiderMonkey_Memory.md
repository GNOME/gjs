# Memory management in SpiderMonkey

When writing JavaScript extensions in C++, we have to understand and be careful about memory management.

This document only applies to C++ code using the jsapi.h API. If you simply write a GObject-style library and describe it via gobject-introspection typelib, there is no need to understand garbage collection details.

## Mark-and-sweep collector

As background, SpiderMonkey uses mark-and-sweep garbage collection. (see [this page][1] for one explanation, if not familiar with this.)

This is a good approach for "embeddable" interpreters, because unlike say the Boehm GC, it doesn't rely on any weird hacks like scanning the entire memory or stack of the process. The collector only has to know about stuff that the language runtime created itself. Also, mark-and-sweep is simple to understand when working with the embedding API.

## Representation of objects

An object has two forms.
* `JS::Value` is a type-tagged version, think of `GValue` (though it is much more efficient)
* inside a `JS::Value` can be one of: a 32-bit integer, a boolean, a double, a `JSString*`, a `JS::Symbol*`, or a `JSObject*`.

`JS::Value` is a 64 bits-wide union. Some of the bits are a type tag. However, don't rely on the layout of `JS::Value`, as it may change between API versions.

You check the type tag with the methods `val.isObject()`, `val.isInt32()`, `val.isDouble()`, `val.isString()`, `val.isBoolean()`, `val.isSymbol()`.
Use `val.isNull()` and `val.isUndefined()` rather than comparing `val == JSVAL_NULL` and `val == JSVAL_VOID` to avoid an extra memory access.

null does not count as an object, so `val.isObject()` does not return true for null. This contrasts with the behavior of `JSVAL_IS_OBJECT(val)`, which was the previous API, but this was changed because the object-or-null behavior was a source of bugs. If you still want this behaviour use `val.isObjectOrNull()`.

The methods `val.toObject()`, `val.toInt32()`, etc. are just accessing the appropriate members of the union.

The jsapi.h header is pretty readable, if you want to learn more. Types you see in there not mentioned above, such as `JSFunction*`, would show up as an object - `val.isObject()` would return true.
From a `JS::Value` perspective, everything is one of object, string, symbol, double, int, boolean, null, or undefined.

## Value types vs. allocated types; "gcthing"

For integers, booleans, doubles, null, and undefined there is no pointer. The value is just part of the `JS::Value` union. So there is no way to "free" these, and no way for them to be finalized or become dangling.

The importance is: these types just get ignored by the garbage collector.

However, strings, symbols, and objects are all allocated pointers that get finalized eventually.
These are what garbage collection applies to.

The API refers to these allocated types as "GC things."
The macro `val.toGCThing()` returns the value part of the union as a pointer.
`val.isGCThing()` returns true for string, object, symbol, null; and false for void, boolean, double, integer.

## Tracing

The general rule is that SpiderMonkey has a set of GC roots. To do the garbage collection, it finds all objects accessible from those roots, and finalizes all objects that are not.

So if you have a `JS::Value` or `JSObject*`/`JSString*`/`JSFunction*`/`JS::Symbol*` somewhere that is not reachable from one of SpiderMonkey's GC roots - say, declared on the stack or in the private data of an object - that will not be found.
SpiderMonkey may try to finalize this object even though you have a reference to it.

If you reference JavaScript objects from your custom object, you have to use `JS::Heap<T>` and set the `JSCLASS_MARK_IS_TRACE` flag in your JSClass, and define a trace function in the class struct. A trace function just invokes `JS::TraceEdge<T>()` to tell SpiderMonkey about any objects you reference. See [JSTraceOp docs][2].

Tracing doesn't add a GC thing to the GC root set!
It just notifies the interpreter that a thing is reachable from another thing.

## Global roots

The GC roots include anything you have declared with `JS::Rooted<T>` and the global object set on each `JSContext*`.
You can also manually add roots with [`JS::PersistentRooted<T>()`][3]. Anything reachable from any of these root objects will not be collected.

`JS::PersistentRooted<T>` pins an object in memory forever until it is destructed, so be careful of leaks. Basically `JS::PersistentRooted<T>` changes memory management of an object to manual mode.

Note that the wrapped T in `JS::PersistentRooted<T>` is the location of your value, not the value itself. That is, a `JSObject**` or `JS::Value*`. Some implications are:
* the location can't go away (don't use a stack address that will vanish before the `JS::PersistentRooted<T>` is destructed, for example)
* the root is keeping "whatever is at the location" from being collected, not "whatever was originally at the location"

## Local roots

Here is the trickier part. If you create an object, say:

```c++
JSObject* obj = JS_NewPlainObject(cx);
```

`obj` is NOT now referenced by any other object. If the GC ran right away, `obj` would be collected.

This is what `JS::Rooted<T>` is for, and its specializations `JS::RootedValue`, `JS::RootedObject`, etc. `JS::Rooted<T>` adds its wrapped `T` to the GC root set, and removes it when the `JS::Rooted<T>` goes out of scope.

Note that `JS::Rooted` can only be used on the stack.
For optimization reasons, roots that are added with `JS::Rooted` must be removed in LIFO order, and the stack scoping ensures that.

Any SpiderMonkey APIs that can cause a garbage collection will force you to use `JS:Rooted<T>` by taking a `JS::Handle<T>` instead of a bare GC thing. `JS::Handle<T>` can only be created from `JS::Rooted<T`>.

So instead of the above code, you would write

```c++
JS::RootedObject obj(cx, JS_NewPlainObject(cx));
```

### JSFunctionSpec and extra local roots

When SpiderMonkey is calling a native function, it will pass in an argv of `JS::Value`. It already has to add all the argv values as GC roots. The "extra local roots" feature tells SpiderMonkey to stick some extra slots on the end of argv that are also GC roots. You can then assign to `argv[MAX(min_args, actual_argc)]` and whatever you put in there won't get garbage collected.

This is kind of a confusing and unreadable hack IMO, though it is probably efficient and thus justified in certain cases. I don't know really.

## More tips

For another attempt to explain all this, see [Rooting Guide from Mozilla.org][4].

[1]: http://www.brpreiss.com/books/opus5/html/page424.html
[2]: http://developer.mozilla.org/en/docs/JSTraceOp
[3]: https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/JSAPI_reference/JS::PersistentRooted
[4]: https://developer.mozilla.org/en-US/docs/Mozilla/Projects/SpiderMonkey/GC_Rooting_Guide "GC"
