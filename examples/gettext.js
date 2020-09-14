// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

imports.gi.versions.Gtk = '3.0';
const Gettext = imports.gettext;
const Gtk = imports.gi.Gtk;

Gettext.bindtextdomain('gnome-panel-3.0', '/usr/share/locale');
Gettext.textdomain('gnome-panel-3.0');

Gtk.init(null);

let w = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
w.add(new Gtk.Label({label: Gettext.gettext('Panel')}));
w.show_all();

Gtk.main();
