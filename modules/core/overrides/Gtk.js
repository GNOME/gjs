// application/javascript;version=1.8
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna

const Legacy = imports._legacy;
const {Gio, GjsPrivate, GObject} = imports.gi;
const {_registerType, definePublicProperties, definePrivateProperties} = imports._common;

let Gtk;
let BuilderScope;

const _hasTemplateSymbol = Symbol('GTK Widget has template');
const _defineChildrenAtInitSymbol = Symbol('GTK Widget assign children to properties at init');

function _hasTemplate(constructor) {
    return constructor.hasOwnProperty(_hasTemplateSymbol) &&
        constructor[_hasTemplateSymbol];
}

function _setHasTemplate(klass) {
    definePrivateProperties(klass, {
        [_hasTemplateSymbol]: true,
    });
}

function _shouldDefineChildrenDuringInit(constructor) {
    return constructor.hasOwnProperty(_defineChildrenAtInitSymbol) &&
        constructor[_defineChildrenAtInitSymbol];
}

function _mapWidgetDefinitionToClass(klass, classDefinition) {
    if ('CssName' in classDefinition)
        klass[Gtk.cssName] = classDefinition.CssName;
    if ('Template' in classDefinition)
        klass[Gtk.template] = classDefinition.Template;
    if ('Children' in classDefinition)
        klass[Gtk.children] = classDefinition.Children;
    if ('InternalChildren' in classDefinition)
        klass[Gtk.internalChildren] = classDefinition.InternalChildren;
}

function _assertDerivesFromWidget(klass, functionName) {
    if (!Gtk.Widget.prototype.isPrototypeOf(klass.prototype)) {
        throw new TypeError(`Gtk.${functionName}() used with invalid base ` +
            `class (is ${Object.getPrototypeOf(klass).name ?? klass})`);
    }
}

function defineChildren(instance, constructor, target = instance) {
    let children = constructor[Gtk.children] || [];
    for (let child of children) {
        target[child.replace(/-/g, '_')] =
            instance.get_template_child(constructor, child);
    }

    let internalChildren = constructor[Gtk.internalChildren] || [];
    for (let child of internalChildren) {
        target[`_${child.replace(/-/g, '_')}`] =
            instance.get_template_child(constructor, child);
    }
}

function _init() {
    Gtk = this;

    Gtk.children = GObject.__gtkChildren__;
    Gtk.cssName = GObject.__gtkCssName__;
    Gtk.internalChildren = GObject.__gtkInternalChildren__;
    Gtk.template = GObject.__gtkTemplate__;

    let {GtkWidgetClass} = Legacy.defineGtkLegacyObjects(GObject, Gtk, _defineChildrenAtInitSymbol);

    // Gtk.Widget instance additions
    definePublicProperties(Gtk.Widget.prototype, {
        _instance_init() {
            if (_hasTemplate(this.constructor))
                this.init_template();
        },
    });

    // Gtk.Widget static overrides
    const {set_template, set_template_from_resource} = Gtk.Widget;

    Object.assign(Gtk.Widget, {
        set_template(contents) {
            set_template.call(this, contents);

            _setHasTemplate(this);
        },
        set_template_from_resource(resource) {
            set_template_from_resource.call(this, resource);

            _setHasTemplate(this);
        },
    });

    // Gtk.Widget static additions
    definePublicProperties(Gtk.Widget, {
        register(classDefinition) {
            _assertDerivesFromWidget(this, 'Widget.register()');

            _mapWidgetDefinitionToClass(this, classDefinition);

            definePrivateProperties(this, {
                [_defineChildrenAtInitSymbol]: false,
            });

            GObject.Object.register.call(this, classDefinition);
        },
    });

    Gtk.Widget.prototype.__metaclass__ = GtkWidgetClass;

    if (Gtk.Container && Gtk.Container.prototype.child_set_property) {
        Gtk.Container.prototype.child_set_property = function (child, property, value) {
            GjsPrivate.gtk_container_child_set_property(this, child, property, value);
        };
    }

    Gtk.Widget.prototype._init = function (params) {
        let wrapper = this;

        if (_hasTemplate(wrapper.constructor)) {
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

        if (_hasTemplate(wrapper.constructor) && _shouldDefineChildrenDuringInit(wrapper.constructor))
            defineChildren(this, wrapper.constructor);

        return wrapper;
    };

    Gtk.Widget._classInit = function (klass) {
        definePrivateProperties(klass, {
            [_defineChildrenAtInitSymbol]: true,
        });

        return GObject.Object._classInit(klass);
    };

    definePrivateProperties(Gtk.Widget, {
        [_registerType]() {
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
        },
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
