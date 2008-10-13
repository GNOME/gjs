/*
 * Copyright (c) 2008  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

// A couple principals of this simple signal system:
// 1) should look just like our GObject signal binding
// 2) memory and safety matter more than speed of connect/disconnect/emit
// 3) the expectation is that a given object will have a very small number of
//    connections, but they may be to different signal names

function _connect(name, callback) {
    // be paranoid about callback arg since we'd start to throw from emit()
    // if it was messed up
    if (typeof(callback) != 'function')
        throw new Error("When connecting signal must give a callback that is a function");

    // we instantiate the "signal machinery" only on-demand if anything
    // gets connected.
    if (!('_signalConnections' in this)) {
        this._signalConnections = [];
        this._nextConnectionId = 1;
    }

    let id = this._nextConnectionId;
    this._nextConnectionId += 1;

    // this makes it O(n) in total connections to emit, but I think
    // it's right to optimize for low memory and reentrancy-safety
    // rather than speed
    this._signalConnections.push({ 'id' : id,
                                   'name' : name,
                                   'callback' : callback,
                                   'disconnected' : false
                                 });
    return id;
}

function _disconnect(id) {
    if ('_signalConnections' in this) {
        let i;
        let length = this._signalConnections.length;
        for (i = 0; i < length; ++i) {
            let connection = this._signalConnections[i];
            if (connection.id == id) {
                if (connection.disconnected)
                    throw new Error("Signal handler id " + id + " already disconnected");

                // set a flag to deal with removal during emission
                connection.disconnected = true;
                this._signalConnections.splice(i, 1);

                return;
            }
        }
    }
    throw new Error("No signal connection " + id + " found");
}

function _disconnectAll() {
    if ('_signalConnections' in this) {
        while (this._signalConnections.length > 0) {
            _disconnect.call(this, this._signalConnections[0].id);
        }
    }
}

function _emit(name /* , arg1, arg2 */) {
    // may not be any signal handlers at all, if not then return
    if (!('_signalConnections' in this))
        return;

    // To deal with re-entrancy (removal/addition while
    // emitting), we copy out a list of what was connected
    // at emission start; and just before invoking each
    // handler we check its disconnected flag.
    let handlers = [];
    let i;
    let length = this._signalConnections.length;
    for (i = 0; i < length; ++i) {
        let connection = this._signalConnections[i];
        if (connection.name == name) {
            handlers.push(connection);
        }
    }

    // create arg array which is emitter + everything passed in except
    // signal name. Would be more convenient not to pass emitter to
    // the callback, but trying to be 100% consistent with GObject
    // which does pass it in. Also if we pass in the emitter here,
    // people don't create closures with the emitter in them,
    // which would be a cycle.

    let arg_array = [ this ];
    // arguments[0] should be signal name so skip it
    length = arguments.length;
    for (i = 1; i < length; ++i) {
        arg_array.push(arguments[i]);
    }

    length = handlers.length;
    for (i = 0; i < length; ++i) {
        let connection = handlers[i];
        if (!connection.disconnected) {
            try {
                // since we pass "null" for this, the global object will be used.
                let ret = connection.callback.apply(null, arg_array);

                // if the callback returns true, we don't call the next
                // signal handlers
                if (ret === true) {
                    break;
                }
            } catch(e) {
                // just log any exceptions so that callbacks can't disrupt
                // signal emission
                logError(e, "Exception in callback for signal: "+name);
            }
        }
    }
}

function addSignalMethods(proto) {
    proto.connect = _connect;
    proto.disconnect = _disconnect;
    proto.emit = _emit;
    // this one is not in GObject, but useful
    proto.disconnectAll = _disconnectAll;
}
