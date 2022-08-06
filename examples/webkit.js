// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

import Gtk from 'gi://Gtk?version=3.0';
import WebKit from 'gi://WebKit2?version=4.0';

Gtk.init(null);

let win = new Gtk.Window();

let view = new WebKit.WebView();
view.load_uri('https://www.gnome.org');
win.add(view);

win.connect('destroy', () => {
    Gtk.main_quit();
});

win.set_size_request(640, 480);
win.show_all();

Gtk.main();

