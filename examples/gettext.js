imports.gi.versions.Gtk = '3.0';
const Gettext = imports.gettext;
const Gtk = imports.gi.Gtk;

Gettext.bindtextdomain("gnome-panel-3.0", "/usr/share/locale");
Gettext.textdomain("gnome-panel-3.0");

Gtk.init(null);

let w = new Gtk.Window({ type: Gtk.WindowType.TOPLEVEL });
w.add(new Gtk.Label({ label: Gettext.gettext("Panel") }));
w.show_all();

if (ARGV.length > 0 && ARGV[0] == "_AUTO_QUIT") {
    this.forceQuit = function() {
        Gtk.main_quit();
    };
    imports.gi.GLib.timeout_add(0, 5000, this.forceQuit);
}

Gtk.main();
