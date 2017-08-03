// application/javascript;version=1.8
// Copyright 2013 Giovanni Campagna
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

const Legacy = imports._legacy;
const GObject = imports.gi.GObject;

var GjsPrivate = imports.gi.GjsPrivate;

let Gtk;

function _init() {

    Gtk = this;

    Gtk.children = GObject._children;
    Gtk.cssName = GObject._cssName;
    Gtk.internalChildren = GObject._internalChildren;
    Gtk.template = GObject._template;

    let {GtkWidgetClass} = Legacy.defineGtkLegacyObjects(GObject, Gtk);
    Gtk.Widget.prototype.__metaclass__ = GtkWidgetClass;

    if (GjsPrivate.gtk_container_child_set_property) {
        Gtk.Container.prototype.child_set_property = function(child, property, value) {
            GjsPrivate.gtk_container_child_set_property(this, child, property, value);
        };
    }

    Gtk.Widget.prototype._init = function(params) {
        GObject.Object.prototype._init.call(this, params);

        if (this.constructor[Gtk.template]) {
            let children = this.constructor[Gtk.children] || [];
            for (let child of children) {
                this[child.replace(/-/g, '_')] =
                    this.get_template_child(this.constructor, child);
            }

            let internalChildren = this.constructor[Gtk.internalChildren] || [];
            for (let child of internalChildren) {
                this['_' + child.replace(/-/g, '_')] =
                    this.get_template_child(this.constructor, child);
            }
        }
    };

    Gtk.Widget._classInit = function(klass) {
        let template = klass[Gtk.template];
        let cssName = klass[Gtk.cssName];
        let children = klass[Gtk.children];
        let internalChildren = klass[Gtk.internalChildren];

        if (template) {
            klass.prototype._instance_init = function() {
                this.init_template();
            };
        }

        klass = GObject.Object._classInit(klass);

        if (cssName)
            Gtk.Widget.set_css_name.call(klass, cssName);

        if (template) {
            if (typeof template === 'string' &&
                template.startsWith('resource:///'))
                Gtk.Widget.set_template_from_resource.call(klass,
                    template.slice(11));
            else
                Gtk.Widget.set_template.call(klass, template);
        }

        if (children) {
            children.forEach(child =>
                Gtk.Widget.bind_template_child_full.call(klass, child, false, 0));
        }

        if (internalChildren) {
            internalChildren.forEach(child =>
                Gtk.Widget.bind_template_child_full.call(klass, child, true, 0));
        }

        return klass;
    };
}
