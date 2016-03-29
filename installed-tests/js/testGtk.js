#!/usr/bin/env gjs

const ByteArray = imports.byteArray;
const Gdk = imports.gi.Gdk;
const Gio = imports.gi.Gio;
const Gtk = imports.gi.Gtk;
const Lang = imports.lang;
const System = imports.system;

const JSUnit = imports.jsUnit;

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
    <child> \
      <object class="GtkLabel" id="label-child2"> \
        <property name="label">Complex as well!</property> \
        <property name="visible">True</property> \
      </object> \
    </child> \
    <child> \
      <object class="GtkLabel" id="internal-label-child"> \
        <property name="label">Complex and internal!</property> \
        <property name="visible">True</property> \
      </object> \
    </child> \
  </template> \
</interface>';

const MyComplexGtkSubclass = new Lang.Class({
    Name: 'MyComplexGtkSubclass',
    Extends: Gtk.Grid,
    Template: ByteArray.fromString(template),
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
    CssName: 'complex-subclass',

    _init: function(params) {
        this.parent(params);

        this._internalLabel = this.get_template_child(MyComplexGtkSubclass, 'label-child');
        JSUnit.assertNotEquals(this._internalLabel, null);

        JSUnit.assertNotEquals(this.label_child2, null);
        JSUnit.assertNotEquals(this._internal_label_child, null);

        JSUnit.assertEquals(Gtk.Widget.get_css_name.call(MyComplexGtkSubclass),
                            'complex-subclass');
    }
});


const MyComplexGtkSubclassFromResource = new Lang.Class({
    Name: 'MyComplexGtkSubclassFromResource',
    Extends: Gtk.Grid,
    Template: 'resource:///org/gjs/jsunit/complex.ui',
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],

    _init: function(params) {
        this.parent(params);

        this._internalLabel = this.label_child;
        JSUnit.assertNotEquals(this.label_child, null);
        JSUnit.assertNotEquals(this.label_child2, null);
        JSUnit.assertNotEquals(this._internal_label_child, null);
    }
});

function validateTemplate(content) {
    let win = new Gtk.Window({ type: Gtk.WindowType.TOPLEVEL });
    win.add(content);

    JSUnit.assertEquals("label is set to 'Complex!'", 'Complex!', content._internalLabel.get_label());
    JSUnit.assertEquals("label is set to 'Complex as well!'", 'Complex as well!', content.label_child2.get_label());
    JSUnit.assertEquals("label is set to 'Complex and internal!'", 'Complex and internal!', content._internal_label_child.get_label());

    win.destroy();
}

function testGtk() {
    Gtk.init(null);

    validateTemplate(new MyComplexGtkSubclass());
    validateTemplate(new MyComplexGtkSubclassFromResource());
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
