// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <gcampagna@src.gnome.org>

imports.gi.versions.Gtk = '3.0';

const ByteArray = imports.byteArray;
const {GLib, Gio, GObject, Gtk} = imports.gi;
const System = imports.system;

// This is ugly here, but usually it would be in a resource
function createTemplate(className) {
    return `
<interface>
  <template class="${className}" parent="GtkGrid">
    <property name="margin_top">10</property>
    <property name="margin_bottom">10</property>
    <property name="margin_start">10</property>
    <property name="margin_end">10</property>
    <property name="visible">True</property>
    <child>
      <object class="GtkLabel" id="label-child">
        <property name="label">Complex!</property>
        <property name="visible">True</property>
        <signal name="grab-focus" handler="templateCallback" swapped="no"/>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="label-child2">
        <property name="label">Complex as well!</property>
        <property name="visible">True</property>
        <signal name="grab-focus" handler="boundCallback" object="label-child" swapped="no"/>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="internal-label-child">
        <property name="label">Complex and internal!</property>
        <property name="visible">True</property>
      </object>
    </child>
  </template>
</interface>`;
}

const MyComplexGtkSubclass = GObject.registerClass({
    Template: ByteArray.fromString(createTemplate('Gjs_MyComplexGtkSubclass')),
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
});

// Sadly, putting this in the body of the class will prevent calling
// get_template_child, since MyComplexGtkSubclass will be bound to the ES6
// class name without the GObject goodies in it
MyComplexGtkSubclass.prototype.testChildrenExist = function () {
    this._internalLabel = this.get_template_child(MyComplexGtkSubclass, 'label-child');
    expect(this._internalLabel).toEqual(jasmine.anything());

    expect(this.label_child2).toEqual(jasmine.anything());
    expect(this._internal_label_child).toEqual(jasmine.anything());
};

const MyComplexGtkSubclassFromResource = GObject.registerClass({
    Template: 'resource:///org/gjs/jsunit/complex3.ui',
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

function validateTemplate(description, ClassName, pending = false) {
    let suite = pending ? xdescribe : describe;
    suite(description, function () {
        let win, content;
        beforeEach(function () {
            win = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
            content = new ClassName();
            content.label_child.emit('grab-focus');
            content.label_child2.emit('grab-focus');
            win.add(content);
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

describe('Gtk overrides', function () {
    beforeAll(function () {
        Gtk.init(null);
    });

    afterAll(function () {
        templateFile.delete(null);
    });

    validateTemplate('UI template', MyComplexGtkSubclass);
    validateTemplate('UI template from resource', MyComplexGtkSubclassFromResource);
    validateTemplate('UI template from file', MyComplexGtkSubclassFromFile);
    validateTemplate('Class inheriting from template class', SubclassSubclass, true);

    it('sets CSS names on classes', function () {
        expect(Gtk.Widget.get_css_name.call(MyComplexGtkSubclass)).toEqual('complex-subclass');
    });

    it('avoid crashing when GTK vfuncs are called in garbage collection', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*during garbage collection*');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*destroy*');

        const BadLabel = GObject.registerClass(class BadLabel extends Gtk.Label {
            vfunc_destroy() {}
        });

        new BadLabel();
        System.gc();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGtk3.js', 0,
            'Gtk overrides avoid crashing and print a stack trace');
    });

    it('GTK vfuncs are not called if the object is disposed', function () {
        const spy = jasmine.createSpy('vfunc_destroy');
        const NotSoGoodLabel = GObject.registerClass(class NotSoGoodLabel extends Gtk.Label {
            vfunc_destroy() {
                spy();
            }
        });

        let label = new NotSoGoodLabel();

        label.destroy();
        expect(spy).toHaveBeenCalledTimes(1);

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*during garbage collection*');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*destroy*');
        label = null;
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGtk3.js', 0,
            'GTK vfuncs are not called if the object is disposed');
    });

    it('destroy signal is emitted while disposing objects', function () {
        const label = new Gtk.Label({label: 'Hello'});
        const handleDispose = jasmine.createSpy('handleDispose').and.callFake(() => {
            expect(label.label).toBe('Hello');
        });
        label.connect('destroy', handleDispose);
        label.destroy();

        expect(handleDispose).toHaveBeenCalledWith(label);

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Label (0x* disposed *');
        expect(label.label).toBe('Hello');
        GLib.test_assert_expected_messages_internal('Gjs', 'testGtk3.js', 0,
            'GTK destroy signal is emitted while disposing objects');
    });

    it('accepts string in place of GdkAtom', function () {
        expect(() => Gtk.Clipboard.get(1)).toThrow();
        expect(() => Gtk.Clipboard.get(true)).toThrow();
        expect(() => Gtk.Clipboard.get(() => undefined)).toThrow();

        const clipboard = Gtk.Clipboard.get('CLIPBOARD');
        const primary = Gtk.Clipboard.get('PRIMARY');
        const anotherClipboard = Gtk.Clipboard.get('CLIPBOARD');

        expect(clipboard).toBeTruthy();
        expect(primary).toBeTruthy();
        expect(clipboard).not.toBe(primary);
        expect(clipboard).toBe(anotherClipboard);
    });

    it('accepts null in place of GdkAtom as GDK_NONE', function () {
        /**
         * When you pass GDK_NONE (an atom, interned from the 'NONE' string)
         * to Gtk.Clipboard.get(), it throws an error, mentioning null in
         * its message.
         */
        expect(() => Gtk.Clipboard.get('NONE')).toThrowError(/null/);

        /**
         * Null is converted to GDK_NONE, so you get the same message. If you
         * know an API function that accepts GDK_NONE without throwing, and
         * returns something different when passed another atom, consider
         * adding a less confusing example here.
         */
        expect(() => Gtk.Clipboard.get(null)).toThrowError(/null/);
    });

    it('uses the correct GType for null child properties', function () {
        let s = new Gtk.Stack();
        let p = new Gtk.Box();

        s.add_named(p, 'foo');
        expect(s.get_child_by_name('foo')).toBe(p);

        s.child_set_property(p, 'name', null);
        expect(s.get_child_by_name('foo')).toBeNull();
    });

    it('can create a Gtk.TreeIter with accessible stamp field', function () {
        const iter = new Gtk.TreeIter();
        iter.stamp = 42;
        expect(iter.stamp).toEqual(42);
    });

    it('can get style properties using GObject.Value', function () {
        let win = new Gtk.ScrolledWindow();
        let value = new GObject.Value();
        value.init(GObject.TYPE_BOOLEAN);
        win.style_get_property('scrollbars-within-bevel', value);
        expect(value.get_boolean()).toBeDefined();

        value.unset();
        value.init(GObject.TYPE_INT);
        let preVal = Math.max(512521, Math.random() * Number.MAX_SAFE_INTEGER);
        value.set_int(preVal);
        win.style_get_property('scrollbar-spacing', value);
        expect(value.get_int()).not.toEqual(preVal);

        win = new Gtk.Window();
        value.unset();
        value.init(GObject.TYPE_STRING);
        value.set_string('EMPTY');
        win.style_get_property('decoration-button-layout', value);
        expect(value.get_string()).not.toEqual('EMPTY');
    });

    it('can pass a parent object to a child at construction', function () {
        const frame = new Gtk.Frame();
        let frameChild = null;
        frame.connect('add', (_widget, child) => {
            frameChild = child;
        });
        const widget = new Gtk.Label({parent: frame});

        expect(widget).toBe(frameChild);
        expect(widget instanceof Gtk.Label).toBeTruthy();
        expect(frameChild instanceof Gtk.Label).toBeTruthy();

        expect(frameChild.visible).toBe(false);
        expect(() => widget.show()).not.toThrow();
        expect(frameChild.visible).toBe(true);
    });
});
