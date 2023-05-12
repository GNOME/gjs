// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Sonny Piers <sonny@fastmail.net>

// This is a simple example of a HTTP client in Gjs using libsoup
// https://developer.gnome.org/libsoup/stable/libsoup-client-howto.html

import Soup from 'gi://Soup?version=3.0';
import GLib from 'gi://GLib';
import Gio from 'gi://Gio';

const loop = GLib.MainLoop.new(null, false);

const session = new Soup.Session();
const message = new Soup.Message({
    method: 'GET',
    uri: GLib.Uri.parse('http://localhost:1080/hello?myname=gjs', GLib.UriFlags.NONE),
});
const decoder = new TextDecoder();

session.send_async(message, null, null, send_async_callback);

function splice_callback(outputStream, result) {
    let data;

    try {
        outputStream.splice_finish(result);
        data = outputStream.steal_as_bytes();
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
    const contentType_ = response_headers.get_one('content-type');

    const outputStream = Gio.MemoryOutputStream.new_resizable();
    outputStream.splice_async(inputStream,
        Gio.OutputStreamSpliceFlags.CLOSE_TARGET,
        GLib.PRIORITY_DEFAULT, null, splice_callback);
}

loop.run();
