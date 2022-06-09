// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

/* exported addSignalMethods, WithSignals */

const Lang = imports.lang;

// Private API, remains exported for backwards compatibility reasons
var {
    _connect, _connectAfter, _disconnect, _emit, _signalHandlerIsConnected,
    _disconnectAll,
} = imports._signals;

// Public API
var {addSignalMethods} = imports._signals;

var WithSignals = new Lang.Interface({
    Name: 'WithSignals',
    connect: _connect,
    connectAfter: _connectAfter,
    disconnect: _disconnect,
    emit: _emit,
    signalHandlerIsConnected: _signalHandlerIsConnected,
    disconnectAll: _disconnectAll,
});
