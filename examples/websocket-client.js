// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Sonny Piers <sonny@fastmail.net>

// This is an example of a WebSocket client in Gjs using libsoup
// https://developer.gnome.org/libsoup/stable/libsoup-2.4-WebSockets.html

const Soup = imports.gi.Soup;
const GLib = imports.gi.GLib;
const byteArray = imports.byteArray;

const loop = GLib.MainLoop.new(null, false);

const session = new Soup.Session();
const message = new Soup.Message({
    method: 'GET',
    uri: Soup.URI.new('wss://echo.websocket.org'),
});

session.websocket_connect_async(message, 'origin', [], null, websocket_connect_async_callback);

function websocket_connect_async_callback(_session, res) {
    let connection;

    try {
        connection = session.websocket_connect_finish(res);
    } catch (e) {
        logError(e);
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

        const str = byteArray.toString(byteArray.fromGBytes(data));
        log(`message: ${str}`);
        connection.close(Soup.WebsocketCloseCode.NORMAL, null);
    });

    log('open');
    connection.send_text('hello');
}

loop.run();
