// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

imports.gi.versions.Gtk = '3.0';
imports.gi.versions.WebKit2 = '4.0';
const Gtk = imports.gi.Gtk;
const WebKit = imports.gi.WebKit2;

Gtk.init(null);

let win = new Gtk.Window();

let view = new WebKit.WebView();
view.load_uri('http://www.google.com/');
win.add(view);

win.connect('destroy', () => {
    Gtk.main_quit();
});

win.set_size_request(640, 480);
win.show_all();

Gtk.main();

