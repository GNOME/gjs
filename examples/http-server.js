// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

// This is a simple example of a HTTP server in GJS using libsoup
// open http://localhost:1080 in your browser or use http-client.js

import Soup from 'gi://Soup?version=3.0';
import GLib from 'gi://GLib';

const loop = GLib.MainLoop.new(null, false);

function handler(_server, msg, _path, _query) {
    msg.set_status(200, null);
    msg.get_response_headers().set_content_type('text/html', {charset: 'UTF-8'});
    msg.get_response_body().append(`
        <html>
        <body>
            Greetings, visitor from ${msg.get_remote_host()}<br>
            What is your name?
            <form action="/hello">
                <input name="myname">
            </form>
        </body>
        </html>
    `);
}

function helloHandler(_server, msg, path, query) {
    if (!query) {
        msg.set_redirect(302, '/');
        return;
    }

    msg.set_status(200, null);
    msg.get_response_headers().set_content_type('text/html', {charset: 'UTF-8'});
    msg.get_response_body().append(`
        <html>
        <body>
            Hello, ${query.myname}! ☺<br>
            <a href="/">Go back</a>
        </body>
        </html>
    `);
}

let server = new Soup.Server();
server.add_handler('/', handler);
server.add_handler('/hello', helloHandler);
server.listen_local(1080, Soup.ServerListenOptions.IPV4_ONLY);

loop.run();
