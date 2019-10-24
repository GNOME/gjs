// @ts-nocheck

// Unstable API
var { _connect, _disconnect, _emit, _signalHandlerIsConnected, _disconnectAll } = imports._signals;

// Public API
var { addSignalMethods } = imports._signals;

const Lang = imports.lang;

var WithSignals = new Lang.Interface({
    Name: 'WithSignals',
    connect: _connect,
    disconnect: _disconnect,
    emit: _emit,
    signalHandlerIsConnected: _signalHandlerIsConnected,
    disconnectAll: _disconnectAll,
});
