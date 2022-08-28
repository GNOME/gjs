# Lang

The `Lang` module is a collection of deprecated features that have been
completely superseded by standard ECMAScript. It remains a part of GJS for
backwards-compatibility reasons, but should never be used in new code.

#### Import

> Attention: This module is not available as an ECMAScript Module

The `Lang` module is available on the global `imports` object:

```js
const Lang = imports.lang
```

### Lang.bind(thisArg, function, ...args)

> Deprecated: Use [`Function.prototype.bind()`][function-bind] instead

Type:
* Static

Parameters:
* thisArg (`Object`) — A JavaScript object
* callback (`Function`) — A function reference
* args (`Any`) — A function reference

Returns:
* (`Function`) — A new `Function` instance, bound to `thisArg`

Binds a function to a scope.

[function-bind]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Global_Objects/Function/bind

### Lang.Class(object)

> Deprecated: Use native [JavaScript Classes][js-class] instead

Type:
* Static

Parameters:
* object (`Object`) — A JavaScript object

Returns:
* (`Object`) — A JavaScript class expression

...

Example usage:

```js
const MyLegacyClass = new Lang.Class({
    _init: function() {
        let fnorb = new FnorbLib.Fnorb();
        fnorb.connect('frobate', Lang.bind(this, this._onFnorbFrobate));
    },

    _onFnorbFrobate: function(fnorb) {
        this._updateFnorb();
    }
});
```

[js-class]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Classes

### Lang.copyProperties(source, dest)

> Deprecated: Use [`Object.assign()`][object-assign] instead

Type:
* Static

Parameters:
* source (`Object`) — The source object
* dest (`Object`) — The target object

Copy all properties from `source` to `dest`, including those that are prefixed
with an underscore (e.g. `_privateFunc()`).

[object-assign]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Global_Objects/Object/assign

### Lang.copyPublicProperties(source, dest)

> Deprecated: Use [`Object.assign()`][object-assign] instead

Type:
* Static

Parameters:
* source (`Object`) — The source object
* dest (`Object`) — The target object

Copy all public properties from `source` to `dest`, excluding those that are
prefixed with an underscore (e.g. `_privateFunc()`).

[object-assign]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Global_Objects/Object/assign

### Lang.countProperties(object)

> Deprecated: Use [`Object.assign()`][object-assign] instead

Type:
* Static

Parameters:
* object (`Object`) — A JavaScript object

[object-assign]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Global_Objects/Object/assign

### Lang.getMetaClass(object)

Type:
* Static

Parameters:
* object (`Object`) — A JavaScript object

Returns:
* (`Object`|`null`) — A `Lang.Class` meta object

...

### Lang.Interface(object)

> Deprecated: Use native [JavaScript Classes][js-class] instead

Type:
* Static

Parameters:
* object (`Object`) — A JavaScript object

Returns:
* (`Object`) — A JavaScript class expression

...

[js-class]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Classes

