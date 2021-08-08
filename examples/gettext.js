// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

/*
 * Make sure you have a non english locale installed, for example fr_FR and run
 * LANGUAGE=fr_FR gjs gettext.js
 * the label should show a translation of 'Print help'
 */

imports.gi.versions.Gtk = '3.0';
const Gettext = imports.gettext;
const Gtk = imports.gi.Gtk;

Gettext.bindtextdomain('gnome-shell', '/usr/share/locale');
Gettext.textdomain('gnome-shell');

Gtk.init(null);

let w = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
w.add(new Gtk.Label({label: Gettext.gettext('Print help')}));
w.show_all();

Gtk.main();
