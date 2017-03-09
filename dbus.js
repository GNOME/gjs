const Gio = imports.gi.Gio;
const Lang = imports.lang;

const App = new Lang.Class({
    Name: 'App',
    Extends: Gio.Application,
    vfunc_activate: function () { this.parent(); },
    vfunc_dbus_unregister: function () {},
});

new App({ application_id: 'com.example.Foo' }).run(['foo']);