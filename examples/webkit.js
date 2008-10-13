const Gtk = imports.gi.Gtk;
const WebKit = imports.gi.WebKit;

Gtk.init(0, null);

let win = new Gtk.Window({type: Gtk.WindowType.toplevel });

let sw = new Gtk.ScrolledWindow({});
win.add(sw);

let view = new WebKit.WebView();
view.open("http://www.google.com/");
sw.add(view);

win.set_size_request(640, 480);
win.show_all();

Gtk.main();

