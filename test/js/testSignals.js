// application/javascript;version=1.8
const Signals = imports.signals;

function Foo() {
    this._init();
}

Foo.prototype = {
    _init : function() {
    }
};

Signals.addSignalMethods(Foo.prototype);

function testSimple() {
    var foo = new Foo();
    var id = foo.connect('bar',
                         function(theFoo, a, b) {
                             theFoo.a = a;
                             theFoo.b = b;
                         });
    foo.emit('bar', "This is a", "This is b");
    assertEquals("This is a", foo.a);
    assertEquals("This is b", foo.b);
    foo.disconnect(id);
    // this emission should do nothing
    foo.emit('bar', "Another a", "Another b");
    // so these values should be unchanged
    assertEquals("This is a", foo.a);
    assertEquals("This is b", foo.b);
}

function testDisconnectDuringEmit() {
    var foo = new Foo();
    var toRemove = [];
    var firstId = foo.connect('bar',
                         function(theFoo) {
                             theFoo.disconnect(toRemove[0]);
                             theFoo.disconnect(toRemove[1]);
                         });
    var id = foo.connect('bar',
                     function(theFoo) {
                         throw new Error("This should not have been called 1");
                     });
    toRemove.push(id);

    id = foo.connect('bar',
                     function(theFoo) {
                         throw new Error("This should not have been called 2");
                     });
    toRemove.push(id);

    // emit signal; what should happen is that the second two handlers are
    // disconnected before they get invoked
    foo.emit('bar');

    // clean up the last handler
    foo.disconnect(firstId);

    // poke in private implementation to sanity-check
    assertEquals('no handlers left', 0, foo._signalConnections.length);
}

function testMultipleSignals() {
    var foo = new Foo();

    foo.barHandlersCalled = 0;
    foo.bonkHandlersCalled = 0;
    foo.connect('bar',
                function(theFoo) {
                    theFoo.barHandlersCalled += 1;
                });
    foo.connect('bonk',
                function(theFoo) {
                    theFoo.bonkHandlersCalled += 1;
                });
    foo.connect('bar',
                function(theFoo) {
                    theFoo.barHandlersCalled += 1;
                });
    foo.emit('bar');

    assertEquals(2, foo.barHandlersCalled);
    assertEquals(0, foo.bonkHandlersCalled);

    foo.emit('bonk');

    assertEquals(2, foo.barHandlersCalled);
    assertEquals(1, foo.bonkHandlersCalled);

    foo.emit('bar');

    assertEquals(4, foo.barHandlersCalled);
    assertEquals(1, foo.bonkHandlersCalled);

    foo.disconnectAll();

    // these post-disconnect emissions should do nothing
    foo.emit('bar');
    foo.emit('bonk');

    assertEquals(4, foo.barHandlersCalled);
    assertEquals(1, foo.bonkHandlersCalled);
}

function testExceptionInCallback() {
    let foo = new Foo();

    foo.bar1Called = 0;
    foo.bar2Called = 0;
    foo.connect('bar',
                function(theFoo) {
                    theFoo.bar1Called += 1;
                    throw new Error("Exception we are throwing on purpose");
                });
    foo.connect('bar',
                function(theFoo) {
                    theFoo.bar2Called += 1;
                });

    // exception in callback does not effect other callbacks
    foo.emit('bar');
    assertEquals(1, foo.bar1Called);
    assertEquals(1, foo.bar2Called);

    // exception in callback does not disconnect the callback
    foo.emit('bar');
    assertEquals(2, foo.bar1Called);
    assertEquals(2, foo.bar2Called);
}

gjstestRun();
