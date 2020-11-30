// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

// This is a simple example of a HTTP server in Gjs using libsoup

const Soup = imports.gi.Soup;

function handler(server, msg, path, query, client) {
    msg.status_code = 200;
    msg.response_headers.set_content_type('text/html', {});
    msg.response_body.append(`
        <html>
        <body>
            Greetings, visitor from ${client.get_host()}<br>
            What is your name?
            <form action="/hello">
                <input name="myname">
            </form>
        </body>
        </html>
    `);
}

function helloHandler(server, msg, path, query) {
    if (!query) {
        msg.set_redirect(302, '/');
        return;
    }

    msg.status_code = 200;
    msg.response_headers.set_content_type('text/html', {charset: 'UTF-8'});
    msg.response_body.append(`
        <html>
        <body>
            Hello, ${query.myname}! ☺<br>
            <a href="/">Go back</a>
        </body>
        </html>
    `);
}

function main() {
    let server = new Soup.Server({port: 1080});
    server.add_handler('/', handler);
    server.add_handler('/hello', helloHandler);
    server.run();
}

main();
