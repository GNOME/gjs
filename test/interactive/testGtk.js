#!/usr/bin/env gjs

const ByteArray = imports.byteArray;
const Gio = imports.gi.Gio;
const Gtk = imports.gi.Gtk;
const Lang = imports.lang;

// This is ugly here, but usually it would be in a resource
const template = ' \
<interface> \
  <template class="Gjs_MyComplexGtkSubclass" parent="GtkGrid"> \
    <property name="margin_top">10</property> \
    <property name="margin_bottom">10</property> \
    <property name="margin_start">10</property> \
    <property name="margin_end">10</property> \
    <property name="visible">True</property> \
    <child> \
      <object class="GtkLabel" id="label-child"> \
        <property name="label">Complex!</property> \
        <property name="visible">True</property> \
      </object> \
    </child> \
  </template> \
</interface>';

const MyComplexGtkSubclass = new Lang.Class({
    Name: 'MyComplexGtkSubclass',
    Extends: Gtk.Grid,
    Template: ByteArray.fromString(template),
    Children: ['label-child'],

    _init: function(params) {
        this.parent(params);

        this._internalLabel = this.get_template_child(MyComplexGtkSubclass, 'label-child');
        log(this._internalLabel);
    }
});

function main() {
    let app = new Gtk.Application({ application_id: 'org.gnome.gjs.TestApplication' });

    app.connect('activate', function() {
        let win = new Gtk.ApplicationWindow({ application: app });
        let content = new MyComplexGtkSubclass();

        win.add(content);
        win.show();
    });

    app.run(null);
}

main();
