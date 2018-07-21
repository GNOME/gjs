#!/usr/bin/env gjs

imports.gi.versions.Gtk = '3.0';

const Gtk = imports.gi.Gtk;
const Mainloop = imports.mainloop;

function nextTitle() {
    let length = Math.random() * 20;
    let str = '';

    for (let i = 0; i < length; i++) {
        str += String.fromCharCode(48 + Math.random() * 79);
    }

    return str;
}

function main() {
    Gtk.init(null);

    let win = new Gtk.Window({ title: nextTitle(), default_width: 400 });
    win.connect('destroy', () => {
        Gtk.main_quit();
    });
    win.present();

    Mainloop.timeout_add(200, function() {
        win.title = nextTitle();
        return true;
    });

    Gtk.main();
}

main();

