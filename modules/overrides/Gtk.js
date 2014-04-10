var GjsPrivate = imports.gi.GjsPrivate;
var Gtk;

function _init() {
    Gtk = this;

    if (GjsPrivate.gtk_container_child_set_property) {
        Gtk.Container.prototype.child_set_property = function(child, property, value) {
            GjsPrivate.gtk_container_child_set_property(this, child, property, value);
        };
    }
}
