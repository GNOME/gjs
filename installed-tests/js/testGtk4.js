// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <gcampagna@src.gnome.org>

imports.gi.versions.Gdk = '4.0';
imports.gi.versions.Gtk = '4.0';

const {Gdk, Gio, GObject, Gtk, GLib, GjsTestTools} = imports.gi;
const System = imports.system;

const PromiseInternal = imports._promiseNative;

// This is ugly here, but usually it would be in a resource
function createTemplate(className) {
    return `
<interface>
  <template class="${className}" parent="GtkGrid">
    <property name="margin_top">10</property>
    <property name="margin_bottom">10</property>
    <property name="margin_start">10</property>
    <property name="margin_end">10</property>
    <child>
      <object class="GtkLabel" id="label-child">
        <property name="label">Complex!</property>
        <signal name="copy-clipboard" handler="templateCallback" swapped="no"/>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="label-child2">
        <property name="label">Complex as well!</property>
        <signal name="copy-clipboard" handler="boundCallback" object="label-child" swapped="no"/>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="internal-label-child">
        <property name="label">Complex and internal!</property>
      </object>
    </child>
  </template>
</interface>`;
}

const MyComplexGtkSubclass = GObject.registerClass({
    Template: new TextEncoder().encode(createTemplate('Gjs_MyComplexGtkSubclass')),
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
    CssName: 'complex-subclass',
}, class MyComplexGtkSubclass extends Gtk.Grid {
    templateCallback(widget) {
        this.callbackEmittedBy = widget;
    }

    boundCallback(widget) {
        widget.callbackBoundTo = this;
    }

    testChildrenExist() {
        this._internalLabel = this.get_template_child(MyComplexGtkSubclass, 'label-child');
        expect(this._internalLabel).toEqual(jasmine.anything());

        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    }
});

const MyComplexGtkSubclassFromResource = GObject.registerClass({
    Template: 'resource:///org/gjs/jsunit/complex4.ui',
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
}, class MyComplexGtkSubclassFromResource extends Gtk.Grid {
    testChildrenExist() {
        expect(this.label_child).toEqual(jasmine.anything());
        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    }

    templateCallback(widget) {
        this.callbackEmittedBy = widget;
    }

    boundCallback(widget) {
        widget.callbackBoundTo = this;
    }
});

const MyComplexGtkSubclassFromString = GObject.registerClass({
    Template: createTemplate('Gjs_MyComplexGtkSubclassFromString'),
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
}, class MyComplexGtkSubclassFromString extends Gtk.Grid {
    testChildrenExist() {
        expect(this.label_child).toEqual(jasmine.anything());
        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    }

    templateCallback(widget) {
        this.callbackEmittedBy = widget;
    }

    boundCallback(widget) {
        widget.callbackBoundTo = this;
    }
});

const [templateFile, stream] = Gio.File.new_tmp(null);
const baseStream = stream.get_output_stream();
const out = new Gio.DataOutputStream({baseStream});
out.put_string(createTemplate('Gjs_MyComplexGtkSubclassFromFile'), null);
out.close(null);

const MyComplexGtkSubclassFromFile = GObject.registerClass({
    Template: templateFile.get_uri(),
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
}, class MyComplexGtkSubclassFromFile extends Gtk.Grid {
    testChildrenExist() {
        expect(this.label_child).toEqual(jasmine.anything());
        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    }

    templateCallback(widget) {
        this.callbackEmittedBy = widget;
    }

    boundCallback(widget) {
        widget.callbackBoundTo = this;
    }
});

const SubclassSubclass = GObject.registerClass(
    class SubclassSubclass extends MyComplexGtkSubclass {});


const CustomActionWidget = GObject.registerClass(
class CustomActionWidget extends Gtk.Widget {
    static _classInit(klass) {
        klass = Gtk.Widget._classInit(klass);

        Gtk.Widget.install_action.call(klass,
            'custom.action',
            null,
            widget => (widget.action = 42));
        return klass;
    }
});

function validateTemplate(description, ClassName, pending = false) {
    let suite = pending ? xdescribe : describe;
    suite(description, function () {
        let win, content;
        beforeEach(function () {
            win = new Gtk.Window();
            content = new ClassName();
            content.label_child.emit('copy-clipboard');
            content.label_child2.emit('copy-clipboard');
            win.set_child(content);
        });

        it('sets up internal and public template children', function () {
            content.testChildrenExist();
        });

        it('sets up public template children with the correct widgets', function () {
            expect(content.label_child.get_label()).toEqual('Complex!');
            expect(content.label_child2.get_label()).toEqual('Complex as well!');
        });

        it('sets up internal template children with the correct widgets', function () {
            expect(content._internal_label_child.get_label())
                .toEqual('Complex and internal!');
        });

        it('connects template callbacks to the correct handler', function () {
            expect(content.callbackEmittedBy).toBe(content.label_child);
        });

        it('binds template callbacks to the correct object', function () {
            expect(content.label_child2.callbackBoundTo).toBe(content.label_child);
        });

        afterEach(function () {
            win.destroy();
        });
    });
}

class LeakTestWidget extends Gtk.Button {
    buttonClicked() {}
}

GObject.registerClass({
    Template: new TextEncoder().encode(`
<interface>
    <template class="Gjs_LeakTestWidget" parent="GtkButton">
        <signal name="clicked" handler="buttonClicked"/>
    </template>
</interface>`),
}, LeakTestWidget);

describe('Gtk 4', function () {
    let writerFunc;
    beforeAll(function () {
        Gtk.init();

        // Set up log writer for tests to override
        writerFunc = jasmine.createSpy('log writer', () => GLib.LogWriterOutput.UNHANDLED);
        writerFunc.and.callThrough();
        GLib.log_set_writer_func(writerFunc);
    });

    afterAll(function () {
        GLib.log_set_writer_default();
        templateFile.delete(null);
    });

    describe('overrides', function () {
        validateTemplate('UI template', MyComplexGtkSubclass);
        validateTemplate('UI template from resource', MyComplexGtkSubclassFromResource);
        validateTemplate('UI template from string', MyComplexGtkSubclassFromString);
        validateTemplate('UI template from file', MyComplexGtkSubclassFromFile);
        validateTemplate('Class inheriting from template class', SubclassSubclass, true);

        describe('Non-template UI file', function () {
            let callbacks;

            beforeEach(function () {
                callbacks = {
                    onButtonClicked() {},
                };
                spyOn(callbacks, 'onButtonClicked');
            });

            it('from resource', function () {
                const builder = new Gtk.Builder({
                    resource: '/org/gjs/jsunit/builder-nontemplate.ui',
                    callbacks,
                });
                const button = builder.get_object('button');
                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalledOnceWith(button);
            });

            it('from filename', function () {
                const [ntFile, ntStream] = Gio.File.new_tmp(null);
                const output = ntStream.get_output_stream();
                const input = Gio.resources_open_stream('/org/gjs/jsunit/builder-nontemplate.ui',
                    Gio.ResourceLookupFlags.NONE);
                output.splice(input,
                    Gio.OutputStreamSpliceFlags.CLOSE_SOURCE | Gio.OutputStreamSpliceFlags.CLOSE_TARGET,
                    null);

                const builder = new Gtk.Builder({
                    filename: ntFile.get_path(),
                    callbacks,
                });
                const button = builder.get_object('button');
                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalledOnceWith(button);

                ntFile.delete(null);
            });

            it('from string', function () {
                const data = Gio.resources_lookup_data('/org/gjs/jsunit/builder-nontemplate.ui',
                    Gio.ResourceLookupFlags.NONE);
                const decoder = new TextDecoder('utf-8');
                const string = decoder.decode(data);
                const builder = new Gtk.Builder({
                    data: string,
                    callbacks,
                });
                const button = builder.get_object('button');
                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalledOnceWith(button);
            });

            it('from bytes', function () {
                const data = Gio.resources_lookup_data('/org/gjs/jsunit/builder-nontemplate.ui',
                    Gio.ResourceLookupFlags.NONE);
                const builder = new Gtk.Builder({
                    data,
                    callbacks,
                });
                const button = builder.get_object('button');
                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalledOnceWith(button);
            });
        });

        describe('UI file with external objects', function () {
            const builderXML = `
                <?xml version="1.0" encoding="UTF-8"?>
                <interface>
                    <object class="GtkWindow" id="window">
                        <child>
                            <object class="GtkButton" id="button">
                                <property name="child">label</property>
                                <signal name="clicked" handler="onButtonClicked" object="obj" swapped="False"/>
                            </object>
                        </child>
                    </object>
                </interface>
            `;

            let callbacks, label, obj;
            beforeEach(function () {
                obj = new GObject.Object();
                label = new Gtk.Label({label: 'Text'});
                callbacks = {
                    onButtonClicked() {},
                };
                spyOn(callbacks, 'onButtonClicked');
            });

            it('fails if external objects not provided', function () {
                expect(() => new Gtk.Builder({data: builderXML})).toThrow();
            });

            it('with objects provided at construct time', function () {
                const builder = new Gtk.Builder({
                    data: builderXML,
                    callbacks,
                    objects: {label, obj},
                });

                const button = builder.get_object('button');
                expect(button.child).toBe(label);

                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalled();
                expect(callbacks.onButtonClicked.calls.mostRecent().object).toBe(obj);
            });

            it('with objects provided using expose_object', function () {
                const builder = new Gtk.Builder({callbacks});
                builder.expose_object('label', label);
                builder.expose_object('obj', obj);
                builder.add_from_string(builderXML, -1);

                const button = builder.get_object('button');
                expect(button.child).toBe(label);

                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalled();
                expect(callbacks.onButtonClicked.calls.mostRecent().object).toBe(obj);
            });

            it('with objects provided using exposeObjects', function () {
                const builder = new Gtk.Builder({callbacks});
                builder.exposeObjects({label, obj});
                builder.add_from_string(builderXML, -1);

                const button = builder.get_object('button');
                expect(button.child).toBe(label);

                button.emit('clicked');
                expect(callbacks.onButtonClicked).toHaveBeenCalled();
                expect(callbacks.onButtonClicked.calls.mostRecent().object).toBe(obj);
            });

            it('lets object be retrieved using get_object', function () {
                const builder = new Gtk.Builder({
                    data: builderXML,
                    callbacks,
                    objects: {label, obj},
                });
                expect(builder.get_object('label')).toBe(label);
                expect(builder.get_object('obj')).toBe(obj);
            });

            it('lets object be retrieved using get_objects', function () {
                const builder = new Gtk.Builder({
                    data: builderXML,
                    callbacks,
                    objects: {label, obj},
                });
                const objects = builder.get_objects();
                expect(objects).toEqual(jasmine.arrayContaining([label, obj]));

                expect(objects.label).toBe(label);
                expect(objects.obj).toBe(obj);
            });
        });

        describe('Gtk.Builder.get_objects override', function () {
            const objectsXML = `
                <?xml version="1.0" encoding="UTF-8"?>
                <interface>
                    <object class="GtkWindow" id="window">
                        <child>
                            <object class="GtkBox" id="box">
                                <child>
                                    <object class="GtkLabel" id="label"/>
                                </child>
                                <child>
                                    <!-- label with in-range integer index ID -->
                                    <object class="GtkLabel" id="0"/>
                                </child>
                                <child>
                                    <!-- label with out-of-range integer index ID -->
                                    <object class="GtkLabel" id="10"/>
                                </child>
                            </object>
                        </child>
                    </object>
                </interface>`;

            let objects, window, box, label, zero, ten;
            beforeEach(function () {
                const builder = new Gtk.Builder({data: objectsXML});
                window = builder.get_object('window');
                box = builder.get_object('box');
                label = builder.get_object('label');
                zero = builder.get_object('0');
                ten = builder.get_object('10');
                objects = builder.get_objects();
            });

            it('works as a drop-in replacement for the original get_objects', function () {
                expect(objects).toEqual(jasmine.arrayContaining([window, box, label, zero, ten]));
            });

            it('allows fetching objects by name', function () {
                expect(objects.window).toBe(window);
                expect(objects.box).toBe(box);
                expect(objects.label).toBe(label);
            });

            it('does not override array index properties', function () {
                expect(objects[0]).not.toBe(zero);
                // OK because the array is not that long
                expect(objects[10]).toBe(ten);
            });

            it('does not duplicate calls to get_object', function () {
                spyOn(Gtk.Builder.prototype, 'get_object').and.callThrough();
                expect(objects.window).toBe(window);
                expect(objects.window).toBe(window);
                expect(Gtk.Builder.prototype.get_object).toHaveBeenCalledTimes(1);
            });

            it('builder instance is kept alive by the proxy', function () {
                System.gc();
                expect(objects.window).toBe(window);
            });

            it('builder instance is not kept alive if no proxy is returned', function () {
                let ref;
                const retrievedWindow = (function () {
                    const builder = new Gtk.Builder({data: objectsXML});
                    ref = new WeakRef(builder);
                    return builder.get_object('window');
                })();
                expect(retrievedWindow).toEqual(jasmine.any(Gtk.Window));
                PromiseInternal.drainMicrotaskQueue();  // let WeakRef lapse
                System.gc();
                expect(ref.deref()).not.toBeDefined();
            });
        });

        it('ensures signal handlers are callable', function () {
            const ClassWithUncallableHandler = GObject.registerClass({
                Template: createTemplate('Gjs_ClassWithUncallableHandler'),
                Children: ['label-child', 'label-child2'],
                InternalChildren: ['internal-label-child'],
            }, class ClassWithUncallableHandler extends Gtk.Grid {
                templateCallback() {}
                get boundCallback() {
                    return 'who ya gonna call?';
                }
            });

            // The exception is thrown inside a vfunc with a GError out parameter,
            // and Gtk logs a critical.
            writerFunc.calls.reset();
            writerFunc.and.callFake((level, fields) => {
                const decoder = new TextDecoder('utf-8');
                const domain = decoder.decode(fields?.GLIB_DOMAIN);
                const message = decoder.decode(fields?.MESSAGE);
                expect(level).toBe(GLib.LogLevelFlags.LEVEL_CRITICAL);
                expect(domain).toBe('Gtk');
                expect(message).toMatch('is not a function');
                return GLib.LogWriterOutput.HANDLED;
            });

            void new ClassWithUncallableHandler();

            expect(writerFunc).toHaveBeenCalled();
            writerFunc.and.callThrough();
        });

        it('rejects unsupported template URIs', function () {
            expect(() => {
                return GObject.registerClass({
                    Template: 'https://gnome.org',
                }, class GtkTemplateInvalid extends Gtk.Widget {
                });
            }).toThrowError(TypeError, /Invalid template URI/);
        });

        it('sets CSS names on classes', function () {
            expect(Gtk.Widget.get_css_name.call(MyComplexGtkSubclass)).toEqual('complex-subclass');
        });

        it('static inheritance works', function () {
            expect(MyComplexGtkSubclass.get_css_name()).toEqual('complex-subclass');
        });

        it('can create a Gtk.TreeIter with accessible stamp field', function () {
            const iter = new Gtk.TreeIter();
            iter.stamp = 42;
            expect(iter.stamp).toEqual(42);
        });

        it('can create a Gtk.CustomSorter with callback', function () {
            const sortFunc = jasmine.createSpy('sortFunc').and.returnValue(1);
            const model = Gtk.StringList.new(['hello', 'world']);
            const sorter = Gtk.CustomSorter.new(sortFunc);
            void Gtk.SortListModel.new(model, sorter);
            expect(sortFunc).toHaveBeenCalledOnceWith(jasmine.any(Gtk.StringObject), jasmine.any(Gtk.StringObject));
        });

        it('can change the callback of a Gtk.CustomSorter', function () {
            const model = Gtk.StringList.new(['hello', 'world']);
            const sorter = Gtk.CustomSorter.new(null);
            void Gtk.SortListModel.new(model, sorter);

            const sortFunc = jasmine.createSpy('sortFunc').and.returnValue(1);
            sorter.set_sort_func(sortFunc);
            expect(sortFunc).toHaveBeenCalledOnceWith(jasmine.any(Gtk.StringObject), jasmine.any(Gtk.StringObject));

            sortFunc.calls.reset();
            sorter.set_sort_func(null);
            expect(sortFunc).not.toHaveBeenCalled();
        });
    });

    describe('regressions', function () {
        it('Gdk.Event fundamental type should not crash', function () {
            expect(() => new Gdk.Event()).toThrowError(/Couldn't find a constructor/);
        });

        xit('Actions added via Gtk.WidgetClass.add_action() should not crash', function () {
            const custom = new CustomActionWidget();
            custom.activate_action('custom.action', null);
            expect(custom.action).toEqual(42);
        }).pend('https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/3796');

        it('Gdk.NoSelection section returns valid start/end values', function () {
            if (!Gtk.NoSelection.prototype.get_section)
                pending('Gtk 4.12 is required');

            let result;
            try {
                result = new Gtk.NoSelection().get_section(0);
            } catch (err) {
                if (err.message.includes('not introspectable'))
                    pending('This version of GTK has the annotation bug');
                throw err;
            }
            expect(result).toEqual([0, GLib.MAXUINT32]);
        });

        function createSurface(shouldStash) {
            // Create a Gdk.Surface that is unreachable after this function ends
            const display = Gdk.Display.get_default();
            const surface = Gdk.Surface.new_toplevel(display);
            if (shouldStash)
                GjsTestTools.save_object(surface);
        }

        it('Gdk.Surface is destroyed properly', function () {
            createSurface(false);
            System.gc();
        });

        it('Gdk.Surface is not destroyed if a ref is held from C', function () {
            createSurface(true);
            System.gc();
            const surface = GjsTestTools.steal_saved();
            expect(surface.is_destroyed()).toBeFalsy();
        });

        it('private type implementing two interfaces is introspected correctly', function () {
            const pages = new Gtk.Notebook().pages;  // implements Gio.ListModel and Gtk.SelectionModel
            expect(pages.get_n_items()).toBe(0);
            expect(pages.get_selection().get_size()).toBe(0);
        });

        it('callback with scope-notify transfer-full in parameter', function () {
            // https://gitlab.gnome.org/GNOME/gjs/-/issues/691
            const model = new Gio.ListStore({itemType: Gtk.Label});
            model.append(new Gtk.Label({label: 'test'}));
            const mapModel = new Gtk.MapListModel({model});
            mapModel.set_map_func(item => Gtk.StringObject.new(item.label));
            mapModel.get_item(0);
        });
    });

    describe('template signal', function () {
        function asyncIdle() {
            return new Promise(resolve => {
                GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                    resolve();
                    return GLib.SOURCE_REMOVE;
                });
            });
        }

        it('does not leak', async function () {
            const weakRef = new WeakRef(new LeakTestWidget());

            await asyncIdle();
            // It takes two GC cycles to free the widget, because of the tardy sweep
            // problem (https://gitlab.gnome.org/GNOME/gjs/-/issues/217)
            System.gc();
            System.gc();

            expect(weakRef.deref()).toBeUndefined();
        });
    });
});
