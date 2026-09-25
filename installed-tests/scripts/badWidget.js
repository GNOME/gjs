// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Canonical, Ltd.

imports.gi.versions.Gtk = '3.0';
const Gtk = imports.gi.Gtk;
const GObject = imports.gi.GObject;
Gtk.init(null);
let BadWidget = GObject.registerClass(class BadWidget extends Gtk.Widget {
    vfunc_destroy() {};
});
new BadWidget();
