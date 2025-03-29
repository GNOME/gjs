// application/javascript;version=1.8
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna

const Legacy = imports._legacy;
const {Gio, GjsPrivate, GLib, GObject} = imports.gi;
const {_createBuilderConnectFunc, _createClosure, _registerType} = imports._common;
const Gi = imports._gi;

let Gtk;
let TemplateBuilderScope;

function _init() {
    Gtk = this;

    Gtk.children = GObject.__gtkChildren__;
    Gtk.cssName = GObject.__gtkCssName__;
    Gtk.internalChildren = GObject.__gtkInternalChildren__;
    Gtk.template = GObject.__gtkTemplate__;

    let {GtkWidgetClass} = Legacy.defineGtkLegacyObjects(GObject, Gtk);
    Gtk.Widget.prototype.__metaclass__ = GtkWidgetClass;

    if (Gtk.Container && Gtk.Container.prototype.child_set_property) {
        Gtk.Container.prototype.child_set_property = function (child, property, value) {
            GjsPrivate.gtk_container_child_set_property(this, child, property, value);
        };
    }

    if (Gtk.CustomSorter) {
        Gtk.CustomSorter.new = GjsPrivate.gtk_custom_sorter_new;
        Gtk.CustomSorter.prototype.set_sort_func = function (sortFunc) {
            GjsPrivate.gtk_custom_sorter_set_sort_func(this, sortFunc);
        };
    }

    Gtk.Widget.prototype._init = function (params) {
        const klass = this.constructor;
        const wrapper = GObject.Object.prototype._init.call(this, params) ?? this;

        if (klass[Gtk.template]) {
            let children = klass[Gtk.children] ?? [];
            for (let child of children) {
                wrapper[child.replace(/-/g, '_')] =
                    wrapper.get_template_child(klass, child);
            }

            let internalChildren = klass[Gtk.internalChildren] ?? [];
            for (let child of internalChildren) {
                wrapper[`_${child.replace(/-/g, '_')}`] =
                    wrapper.get_template_child(klass, child);
            }
        }

        return wrapper;
    };

    Gtk.Widget._classInit = function (klass) {
        return GObject.Object._classInit(klass);
    };

    Object.defineProperty(Gtk.Widget, _registerType, {
        value: _registerWidgetType,
        writable: false,
        configurable: false,
        enumerable: false,
    });

    if (Gtk.Widget.prototype.get_first_child) {
        Gtk.Widget.prototype[Symbol.iterator] = function* () {
            for (let c = this.get_first_child(); c; c = c.get_next_sibling())
                yield c;
        };
    }

    // Everything after this is GTK4-only, for the Gtk.Builder JS implementation
    if (!Gtk.BuilderScope)
        return;

    TemplateBuilderScope = GObject.registerClass({
        Implements: [Gtk.BuilderScope],
    }, class extends GObject.Object {
        vfunc_create_closure(builder, handlerName, flags, connectObject) {
            const swapped = flags & Gtk.BuilderClosureFlags.SWAPPED;
            const thisArg = builder.get_current_object();
            return Gi.associateClosure(
                connectObject ?? thisArg,
                _createClosure(builder, thisArg, handlerName, swapped, connectObject)
            );
        }
    });
}

function _registerWidgetType(klass) {
    const template = klass[Gtk.template];
    const cssName = klass[Gtk.cssName];
    const children = klass[Gtk.children];
    const internalChildren = klass[Gtk.internalChildren];

    if (template) {
        klass.prototype._instance_init = function () {
            this.init_template();
        };
    }

    GObject.Object[_registerType](klass);

    if (cssName)
        Gtk.Widget.set_css_name.call(klass, cssName);

    if (template) {
        if (typeof template === 'string') {
            try {
                const uri = GLib.Uri.parse(template, GLib.UriFlags.NONE);
                const scheme = uri.get_scheme();

                if (scheme === 'resource') {
                    Gtk.Widget.set_template_from_resource.call(klass, uri.get_path());
                } else if (scheme === 'file') {
                    const file = Gio.File.new_for_uri(template);
                    const [, contents] = file.load_contents(null);
                    Gtk.Widget.set_template.call(klass, contents);
                } else {
                    throw new TypeError(`Invalid template URI: ${template}`);
                }
            } catch (err) {
                if (!(err instanceof GLib.UriError))
                    throw err;

                const contents = new TextEncoder().encode(template);
                Gtk.Widget.set_template.call(klass, contents);
            }
        } else {
            Gtk.Widget.set_template.call(klass, template);
        }

        if (TemplateBuilderScope)
            Gtk.Widget.set_template_scope.call(klass, new TemplateBuilderScope());
        else
            Gtk.Widget.set_connect_func.call(klass, _createBuilderConnectFunc(klass));
    }

    if (children) {
        children.forEach(child =>
            Gtk.Widget.bind_template_child_full.call(klass, child, false, 0));
    }

    if (internalChildren) {
        internalChildren.forEach(child =>
            Gtk.Widget.bind_template_child_full.call(klass, child, true, 0));
    }
}
