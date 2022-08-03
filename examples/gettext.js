// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

/*
 * Make sure you have a non english locale installed, for example fr_FR and run
 * LANGUAGE=fr_FR gjs -m gettext.js
 * the label should show a translation of 'Print help'
 */

import Gettext, {gettext as _} from 'gettext';
import Gtk from 'gi://Gtk?version=4.0';
import GLib from 'gi://GLib';

Gtk.init();

let loop = GLib.MainLoop.new(null, false);

Gettext.bindtextdomain('gnome-shell', '/usr/share/locale');
Gettext.textdomain('gnome-shell');

let window = new Gtk.Window({title: 'gettext'});
window.set_child(new Gtk.Label({label: _('Print help')}));
window.connect('close-request', () => {
    loop.quit();
});

window.present();

loop.run();
