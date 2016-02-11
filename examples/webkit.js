const Gtk = imports.gi.Gtk;
const WebKit = imports.gi.WebKit2;

Gtk.init(null);

let win = new Gtk.Window();

let view = new WebKit.WebView();
view.load_uri("http://www.google.com/");
win.add(view);

win.set_size_request(640, 480);
win.show_all();

Gtk.main();

