
import {Class, Interface, getMetaClass} from './_legacy.js';
import {
    _connect,
    _disconnect,
    _emit,
    _signalHandlerIsConnected,
    _disconnectAll,
    addSignalMethods
} from './signals.js';
import {format, vprintf, printf} from './format.js';

if ('imports' in globalThis) {
    // eslint-disable-next-line no-restricted-properties
    Object.assign(imports.format, {format, vprintf, printf});
    Object.assign(imports.lang, {Class, Interface, getMetaClass});
    Object.assign(imports.signals, {
        _connect,
        _disconnect,
        _emit,
        _signalHandlerIsConnected,
        _disconnectAll,
        addSignalMethods,
    });

    const Lang = imports.lang;

    const WithSignals = new Lang.Interface({
        Name: 'WithSignals',
        connect: _connect,
        disconnect: _disconnect,
        emit: _emit,
        signalHandlerIsConnected: _signalHandlerIsConnected,
        disconnectAll: _disconnectAll,
    });

    Object.assign(imports.signals, {WithSignals});
}
