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

const Lang = imports.lang;
const GObject = imports.gi.GObject;

var GjsPrivate = imports.gi.GjsPrivate;

let Gtk;

const GtkWidgetClass = new Lang.Class({
    Name: 'GtkWidgetClass',
    Extends: GObject.Class,

    _init: function(params) {
        let template = params.Template;
        delete params.Template;

        let children = params.Children;
        delete params.Children;

        let internalChildren = params.InternalChildren;
        delete params.InternalChildren;

        let cssName = params.CssName;
        delete params.CssName;

        if (template) {
            params._instance_init = function() {
                this.init_template();
            };
        }

        this.parent(params);

        if (cssName)
            Gtk.Widget.set_css_name.call(this, cssName);

        if (template) {
            if (typeof template == 'string' &&
                template.startsWith('resource:///'))
                Gtk.Widget.set_template_from_resource.call(this, template.slice(11));
            else
                Gtk.Widget.set_template.call(this, template);
        }

        this.Template = template;
        this.Children = children;
        this.InternalChildren = internalChildren;

        if (children) {
            for (let i = 0; i < children.length; i++)
                Gtk.Widget.bind_template_child_full.call(this, children[i], false, 0);
        }

        if (internalChildren) {
            for (let i = 0; i < internalChildren.length; i++)
                Gtk.Widget.bind_template_child_full.call(this, internalChildren[i], true, 0);
        }
    },

    _isValidClass: function(klass) {
        let proto = klass.prototype;

        if (!proto)
            return false;

        // If proto == Gtk.Widget.prototype, then
        // proto.__proto__ is GObject.InitiallyUnowned, so
        // "proto instanceof Gtk.Widget"
        // will return false.
        return proto == Gtk.Widget.prototype ||
            proto instanceof Gtk.Widget;
    },
});

function _init() {

    Gtk = this;

    Gtk.Widget.prototype.__metaclass__ = GtkWidgetClass;
    if (GjsPrivate.gtk_container_child_set_property) {
        Gtk.Container.prototype.child_set_property = function(child, property, value) {
            GjsPrivate.gtk_container_child_set_property(this, child, property, value);
        };
    }

    Gtk.Widget.prototype._init = function(params) {
        GObject.Object.prototype._init.call(this, params);

        if (this.constructor.Template) {
            let children = this.constructor.Children || [];
            for (let child of children)
                this[child.replace('-', '_', 'g')] = this.get_template_child(this.constructor, child);

            let internalChildren = this.constructor.InternalChildren || [];
            for (let child of internalChildren)
                this['_' + child.replace('-', '_', 'g')] = this.get_template_child(this.constructor, child);
        }
    };
}
