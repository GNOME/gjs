// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC

import GLib from 'gi://GLib';
import Gio from 'gi://Gio';

let loop = GLib.MainLoop.new(null, false);
const decoder = new TextDecoder();

function cat(filename) {
    let f = Gio.file_new_for_path(filename);
    f.load_contents_async(null, (obj, res) => {
        let contents;
        try {
            contents = obj.load_contents_finish(res)[1];
        } catch (err) {
            logError(err);
            loop.quit();
            return;
        }
        print(decoder.decode(contents));
        loop.quit();
    });

    loop.run();
}

if (ARGV.length !== 1)
    printerr('Usage: gio-cat.js filename');
else
    cat(ARGV[0]);
