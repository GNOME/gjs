// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Sonny Piers <sonnyp@gnome.org>

import Gio from 'gi://Gio';
import Gtk from 'gi://Gtk?version=4.0';
import System from 'system';

Gtk.init();

const file = Gio.File.new_for_uri(import.meta.url).resolve_relative_path('../gtk4-interface.ui');

const app = new Gtk.Application({
    application_id: 'hello.world',
});

function onclicked(button) {
    console.log('Hello', button.label);
    button.get_root().close();
}

app.connect('activate', () => {
    const builder = new Gtk.Builder({
        filename: file.get_path(),
        callbacks: {onclicked},
        objects: {app},
    });
    const {window, button} = builder.get_objects();
    button.label = 'Naturaleza es maravillosa';
    window.present();
});

app.run([System.programInvocationName].concat(ARGV));
