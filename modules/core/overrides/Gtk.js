// application/javascript;version=1.8
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna

const Legacy = imports._legacy;
const {Gio, GjsPrivate, GObject} = imports.gi;
const {_registerType} = imports._common;

let Gtk;
let BuilderScope;

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
        let wrapper = this;

        if (wrapper.constructor[Gtk.template]) {
            if (!BuilderScope) {
                Gtk.Widget.set_connect_func.call(wrapper.constructor,
                    (builder, obj, signalName, handlerName, connectObj, flags) => {
                        const swapped = flags & GObject.ConnectFlags.SWAPPED;
                        const closure = _createClosure(
                            builder, wrapper, handlerName, swapped, connectObj);

                        if (flags & GObject.ConnectFlags.AFTER)
                            obj.connect_after(signalName, closure);
                        else
                            obj.connect(signalName, closure);
                    });
            }
        }

        wrapper = GObject.Object.prototype._init.call(wrapper, params) ?? wrapper;

        if (wrapper.constructor[Gtk.template]) {
            let children = wrapper.constructor[Gtk.children] || [];
            for (let child of children) {
                wrapper[child.replace(/-/g, '_')] =
                    wrapper.get_template_child(wrapper.constructor, child);
            }

            let internalChildren = wrapper.constructor[Gtk.internalChildren] || [];
            for (let child of internalChildren) {
                wrapper[`_${child.replace(/-/g, '_')}`] =
                    wrapper.get_template_child(wrapper.constructor, child);
            }
        }

        return wrapper;
    };

    Gtk.Widget._classInit = function (klass) {
        return GObject.Object._classInit(klass);
    };

    function registerWidgetType() {
        let klass = this;

        let template = klass[Gtk.template];
        let cssName = klass[Gtk.cssName];
        let children = klass[Gtk.children];
        let internalChildren = klass[Gtk.internalChildren];

        if (template) {
            klass.prototype._instance_init = function () {
                this.init_template();
            };
        }

        GObject.Object[_registerType].call(klass);

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
            } else {
                Gtk.Widget.set_template.call(klass, template);
            }

            if (BuilderScope)
                Gtk.Widget.set_template_scope.call(klass, new BuilderScope());
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

    Object.defineProperty(Gtk.Widget, _registerType, {
        value: registerWidgetType,
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

    if (Gtk.BuilderScope) {
        BuilderScope = GObject.registerClass({
            Implements: [Gtk.BuilderScope],
        }, class extends GObject.Object {
            vfunc_create_closure(builder, handlerName, flags, connectObject) {
                const swapped = flags & Gtk.BuilderClosureFlags.SWAPPED;
                return _createClosure(
                    builder, builder.get_current_object(),
                    handlerName, swapped, connectObject);
            }
        });
    }
}

function _createClosure(builder, thisArg, handlerName, swapped, connectObject) {
    connectObject = connectObject || thisArg;

    if (swapped) {
        throw new Error('Unsupported template signal flag "swapped"');
    } else if (typeof thisArg[handlerName] === 'undefined') {
        throw new Error(`A handler called ${handlerName} was not ` +
            `defined on ${thisArg}`);
    }

    return thisArg[handlerName].bind(connectObject);
}
