# Signals

The `Signals` module provides a GObject-like signal framework for native
JavaScript classes and objects.

Example usage:

```js
const Signals = imports.signals;

// Apply signal methods to a class prototype
var ExampleObject = class {
    emitExampleSignal () {
        this.emit('exampleSignal', 'stringArg', 42);
    }
}
Signals.addSignalMethods(ExampleObject.prototype);

const obj = new ExampleObject();

// Connect to a signal
const handlerId = obj.connect('exampleSignal', (obj, stringArg, intArg) => {
    // Note that `this` always refers `globalThis` in a Signals callback
});

// Disconnect a signal handler
obj.disconnect(handlerId);
```

#### Import

> Attention: This module is not available as an ECMAScript Module

The `Signals` module is available on the global `imports` object:

```js
const Signals = imports.signals;
```

### Signals.addSignalMethods(object)

Type:
* Static

Parameters:
* object (`Object`) — A JavaScript object

Applies the `Signals` convenience methods to an `Object`.

Generally, this is called on an object prototype, but may also be called on an
object instance.

### connect(name, callback)

> Warning: Unlike GObject signals, `this` within a signal callback will always
> refer to the global object (ie. `globalThis`).

Parameters:
* name (`String`) — A signal name
* callback (`Function`) — A callback function

Returns:
* (`Number`) — A handler ID

Connects a callback to a signal for an object. Pass the returned ID to
`disconnect()` to remove the handler.

If `callback` returns `true`, emission will stop and no other handlers will be
invoked.

### disconnect(id)

Parameters:
* id (`Number`) — The ID of the handler to be disconnected

Disconnects a handler for a signal.

### disconnectAll()

Disconnects all signal handlers for an object.

### emit(name, ...args)

Parameters:
* name (`String`) — A signal name
* args (`Any`) — Any number of arguments, of any type

Emits a signal for an object. Emission stops if a signal handler returns `true`.

Unlike GObject signals, it is not necessary to declare signals or define their
signature. Simply call `emit()` with whatever signal name you wish, with
whatever arguments you wish.

### signalHandlerIsConnected(id)

Parameters:
* id (`Number`) — The ID of the handler to be disconnected

Returns:
* (`Boolean`) — `true` if connected, or `false` if not

Checks if a handler ID is connected.

