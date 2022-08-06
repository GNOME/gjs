// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Sonny Piers <sonny@fastmail.net>

// This is a simple example of a HTTP client in Gjs using libsoup
// https://developer.gnome.org/libsoup/stable/libsoup-client-howto.html

import Soup from 'gi://Soup?version=3.0';
import GLib from 'gi://GLib';

const loop = GLib.MainLoop.new(null, false);

const session = new Soup.Session();
const message = new Soup.Message({
    method: 'GET',
    uri: GLib.Uri.parse('http://localhost:1080/hello?myname=gjs', GLib.UriFlags.NONE),
});
const decoder = new TextDecoder();

session.send_async(message, null, null, send_async_callback);

function read_bytes_async_callback(inputStream, res) {
    let data;

    try {
        data = inputStream.read_bytes_finish(res);
    } catch (err) {
        logError(err);
        loop.quit();
        return;
    }

    console.log('body:', decoder.decode(data.toArray()));

    loop.quit();
}

function send_async_callback(self, res) {
    let inputStream;

    try {
        inputStream = session.send_finish(res);
    } catch (err) {
        logError(err);
        loop.quit();
        return;
    }

    console.log('status:', message.status_code, message.reason_phrase);

    const response_headers = message.get_response_headers();
    response_headers.foreach((name, value) => {
        console.log(name, ':', value);
    });

    inputStream.read_bytes_async(response_headers.get_one('content-length'), null, null, read_bytes_async_callback);
}

loop.run();
