// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Sonny Piers <sonny@fastmail.net>

// This is a simple example of a HTTP client in Gjs using libsoup
// https://developer.gnome.org/libsoup/stable/libsoup-client-howto.html

const Soup = imports.gi.Soup;
const GLib = imports.gi.GLib;
const byteArray = imports.byteArray;

const loop = GLib.MainLoop.new(null, false);

const session = new Soup.Session();
const message = new Soup.Message({
    method: 'GET',
    uri: Soup.URI.new('http://localhost:1080/hello?myname=gjs'),
});

session.send_async(message, null, send_async_callback);

function read_bytes_async_callback(inputStream, res) {
    let data;

    try {
        data = inputStream.read_bytes_finish(res);
    } catch (e) {
        logError(e);
        loop.quit();
        return;
    }

    log(`body:\n${byteArray.toString(byteArray.fromGBytes(data))}`);

    loop.quit();
}

function send_async_callback(self, res) {
    let inputStream;

    try {
        inputStream = session.send_finish(res);
    } catch (e) {
        logError(e);
        loop.quit();
        return;
    }

    log(`status: ${message.status_code} - ${message.reason_phrase}`);
    message.response_headers.foreach((name, value) => {
        log(`${name}: ${value}`);
    });

    inputStream.read_bytes_async(message.response_headers.get('content-length'), null, null, read_bytes_async_callback);
}

loop.run();
