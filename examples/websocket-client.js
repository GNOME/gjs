// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Sonny Piers <sonny@fastmail.net>

// This is an example of a WebSocket client in Gjs using libsoup
// https://developer.gnome.org/libsoup/stable/libsoup-2.4-WebSockets.html

import Soup from 'gi://Soup?version=3.0';
import GLib from 'gi://GLib';

const loop = GLib.MainLoop.new(null, false);

const session = new Soup.Session();
const message = new Soup.Message({
    method: 'GET',
    uri: GLib.Uri.parse('wss://ws.postman-echo.com/raw', GLib.UriFlags.NONE),
});
const decoder = new TextDecoder();

session.websocket_connect_async(message, null, [], null, null, websocket_connect_async_callback);

function websocket_connect_async_callback(_session, res) {
    let connection;

    try {
        connection = session.websocket_connect_finish(res);
    } catch (err) {
        logError(err);
        loop.quit();
        return;
    }

    connection.connect('closed', () => {
        log('closed');
        loop.quit();
    });

    connection.connect('error', (self, err) => {
        logError(err);
        loop.quit();
    });

    connection.connect('message', (self, type, data) => {
        if (type !== Soup.WebsocketDataType.TEXT)
            return;

        const str = decoder.decode(data.toArray());
        log(`message: ${str}`);
        connection.close(Soup.WebsocketCloseCode.NORMAL, null);
    });

    log('open');
    connection.send_text('hello');
}

loop.run();
