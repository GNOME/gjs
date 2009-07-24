const Gettext = imports.gettext;
const Gtk = imports.gi.Gtk;
const Mainloop = imports.mainloop;

Gettext.bindtextdomain("gnome-panel-2.0", "/usr/share/locale");
Gettext.textdomain("gnome-panel-2.0");

Gtk.init(0, null);

let w = new Gtk.Window({ type: Gtk.WindowType.TOPLEVEL });
w.add(new Gtk.Label({ label: Gettext.gettext("Panel") }));
w.show_all();

Mainloop.run("main");
