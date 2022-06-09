// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2022 Canonical Ltd.
// SPDX-FileContributor: Marco Trevisan <marco.trevisan@canonical.com>

/* exported addSignalMethods */

// A couple principals of this simple signal system:
// 1) should look just like our GObject signal binding
// 2) memory and safety matter more than speed of connect/disconnect/emit
// 3) the expectation is that a given object will have a very small number of
//    connections, but they may be to different signal names

function _connect(name, callback) {
    // be paranoid about callback arg since we'd start to throw from emit()
    // if it was messed up
    if (typeof callback !== 'function')
        throw new Error('When connecting signal must give a callback that is a function');

    // we instantiate the "signal machinery" only on-demand if anything
    // gets connected.
    if (this._signalConnections === undefined) {
        this._signalConnections = [];
        this._nextConnectionId = 1;
    }

    const id = this._nextConnectionId;
    this._nextConnectionId += 1;

    // this makes it O(n) in total connections to emit, but I think
    // it's right to optimize for low memory and reentrancy-safety
    // rather than speed
    this._signalConnections.push({
        id,
        name,
        callback,
    });
    return id;
}

function _disconnect(id) {
    const connectionIdx = this._signalConnections?.findIndex(c => c.id === id);

    if (connectionIdx === undefined || connectionIdx === -1)
        throw new Error(`No signal connection ${id} found`);

    const connection = this._signalConnections[connectionIdx];
    if (connection.disconnected)
        throw new Error(`Signal handler id ${id} already disconnected`);

    connection.disconnected = true;
    this._signalConnections.splice(connectionIdx, 1);
}

function _signalHandlerIsConnected(id) {
    return !!this._signalConnections?.some(c => c.id === id && !c.disconnected);
}

function _disconnectAll() {
    this._signalConnections?.forEach(c => (c.disconnected = true));
    delete this._signalConnections;
}

function _emit(name, ...args) {
    // may not be any signal handlers at all, if not then return
    if (this._signalConnections === undefined)
        return;

    // To deal with re-entrancy (removal/addition while
    // emitting), we copy out a list of what was connected
    // at emission start; and just before invoking each
    // handler we check its disconnected flag.
    const handlers = this._signalConnections.filter(c => c.name === name);

    // create arg array which is emitter + everything passed in except
    // signal name. Would be more convenient not to pass emitter to
    // the callback, but trying to be 100% consistent with GObject
    // which does pass it in. Also if we pass in the emitter here,
    // people don't create closures with the emitter in them,
    // which would be a cycle.
    const argArray = [this, ...args];

    for (const handler of handlers) {
        if (handler.disconnected)
            continue;

        try {
            // since we pass "null" for this, the global object will be used.
            const ret = handler.callback.apply(null, argArray);

            // if the callback returns true, we don't call the next
            // signal handlers
            if (ret === true)
                break;
        } catch (e) {
            // just log any exceptions so that callbacks can't disrupt
            // signal emission
            logError(e, `Exception in callback for signal: ${handler.name}`);
        }
    }
}

function _addSignalMethod(proto, functionName, func) {
    if (proto[functionName] && proto[functionName] !== func)
        log(`WARNING: addSignalMethods is replacing existing ${proto} ${functionName} method`);

    proto[functionName] = func;
}

function addSignalMethods(proto) {
    _addSignalMethod(proto, 'connect', _connect);
    _addSignalMethod(proto, 'disconnect', _disconnect);
    _addSignalMethod(proto, 'emit', _emit);
    _addSignalMethod(proto, 'signalHandlerIsConnected', _signalHandlerIsConnected);
    // this one is not in GObject, but useful
    _addSignalMethod(proto, 'disconnectAll', _disconnectAll);
}
