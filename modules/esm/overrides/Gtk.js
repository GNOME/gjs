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

import Gio from "gi://Gio";
import GObject from "gi://GObject";
import GjsPrivate from "gi://GjsPrivate";
import Gtk from "gi://Gtk?v=3";

Gtk.children = GObject.__gtkChildren__;
Gtk.cssName = GObject.__gtkCssName__;
Gtk.internalChildren = GObject.__gtkInternalChildren__;
Gtk.template = GObject.__gtkTemplate__;

if (GjsPrivate.gtk_container_child_set_property) {
    Gtk.Container.prototype.child_set_property = function (child, property, value) {
        GjsPrivate.gtk_container_child_set_property(this, child, property, value);
    };
}

Gtk.Widget.prototype._init = function (params) {
    if (this.constructor[Gtk.template]) {
        Gtk.Widget.set_connect_func.call(this.constructor, (builder, obj, signalName, handlerName, connectObj, flags) => {
            if (connectObj !== null) {
                throw new Error('Unsupported template signal attribute "object"');
            } else if (flags & GObject.ConnectFlags.SWAPPED) {
                throw new Error('Unsupported template signal flag "swapped"');
            } else if (flags & GObject.ConnectFlags.AFTER) {
                obj.connect_after(signalName, this[handlerName].bind(this));
            } else {
                obj.connect(signalName, this[handlerName].bind(this));
            }
        });
    }

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

Gtk.Widget._classInit = function (klass) {
    let template = klass[Gtk.template];
    let cssName = klass[Gtk.cssName];
    let children = klass[Gtk.children];
    let internalChildren = klass[Gtk.internalChildren];

    if (template) {
        klass.prototype._instance_init = function () {
            this.init_template();
        };
    }

    klass = GObject.Object._classInit(klass);

    if (cssName)
        Gtk.Widget.set_css_name.call(klass, cssName);

    if (template) {
        if (typeof template === 'string') {
            if (template.startsWith('resource:///')) {
                Gtk.Widget.set_template_from_resource.call(klass,
                    template.slice(11));
            } else if (template.startsWith('file:///')) {
                let file = Gio.File.new_for_uri(template);
                let [, contents] = file.load_contents(null);
                Gtk.Widget.set_template.call(klass, contents);
            }
        } else
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
