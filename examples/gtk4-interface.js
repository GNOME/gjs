// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Sonny Piers <sonnyp@gnome.org>

import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import System from 'system';


Gtk.init();

const uri = GLib.Uri.resolve_relative(import.meta.url, 'gtk4-interface.ui', GLib.UriFlags.NONE);

const app = new Gtk.Application({
    application_id: 'hello.world',
});

function onclicked(button) {
    console.log('Hello', button.label);
    button.get_root().close();
}

app.connect('activate', () => {
    const {window, button} = Gtk.build(uri, {
        onclicked,
        app,
    });
    button.label = 'Naturaleza es maravillosa';
    window.present();
});

app.run([System.programInvocationName].concat(ARGV));
