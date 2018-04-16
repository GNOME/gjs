imports.gi.versions.Gtk = '3.0';

const ByteArray = imports.byteArray;
const {GLib, GObject, Gtk} = imports.gi;
const System = imports.system;

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
        <signal name="grab-focus" handler="templateCallback" swapped="no"/> \
        <signal name="grab-focus" handler="templateObjCallback" object="Gjs_MyComplexGtkSubclass" swapped="no"/> \
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

const MyComplexGtkSubclass = GObject.registerClass({
    Template: ByteArray.fromString(template),
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
    CssName: 'complex-subclass',
}, class MyComplexGtkSubclass extends Gtk.Grid {});

// Sadly, putting this in the body of the class will prevent calling
// get_template_child, since MyComplexGtkSubclass will be bound to the ES6
// class name without the GObject goodies in it
MyComplexGtkSubclass.prototype.testChildrenExist = function() {
    this._internalLabel = this.get_template_child(MyComplexGtkSubclass, 'label-child');
    expect(this._internalLabel).toEqual(jasmine.anything());

    expect(this.label_child2).toEqual(jasmine.anything());
    expect(this._internal_label_child).toEqual(jasmine.anything());
};

MyComplexGtkSubclass.prototype.templateCallback = function(widget) {
    this.templateChildEmitter = widget;
};

MyComplexGtkSubclass.prototype.templateObjCallback = function(widget) {
    this.templateObjChildEmitter = widget;
};

const MyComplexGtkSubclassFromResource = GObject.registerClass({
    Template: 'resource:///org/gjs/jsunit/complex.ui',
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
}, class MyComplexGtkSubclassFromResource extends Gtk.Grid {
    testChildrenExist() {
        expect(this.label_child).toEqual(jasmine.anything());
        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    }

    templateCallback(widget) {
        this.templateChildEmitter = widget;
    }

    templateObjCallback(widget) {
        this.templateObjChildEmitter = widget;
    }
});

const SubclassSubclass = GObject.registerClass(
    class SubclassSubclass extends MyComplexGtkSubclass {});

function validateTemplate(description, ClassName, pending=false) {
    let suite = pending ? xdescribe : describe;
    suite(description, function () {
        let win, content;
        beforeEach(function () {
            win = new Gtk.Window({ type: Gtk.WindowType.TOPLEVEL });
            content = new ClassName();
            content.label_child.emit('grab-focus');
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

        it('connects template callbacks to the correct handler without a connect object', function () {
            expect(content.templateChildEmitter)
                .toBe(content.label_child);
        });

        it('connects template callbacks to the correct handler with a connect object', function () {
            expect(content.templateObjChildEmitter)
                .toBe(content.label_child);
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

    validateTemplate('UI template', MyComplexGtkSubclass);
    validateTemplate('UI template from resource', MyComplexGtkSubclassFromResource);
    validateTemplate('Class inheriting from template class', SubclassSubclass, true);

    it('sets CSS names on classes', function () {
        expect(Gtk.Widget.get_css_name.call(MyComplexGtkSubclass)).toEqual('complex-subclass');
    });

    it('avoid crashing when GTK vfuncs are called in garbage collection', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*during garbage collection*');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*destroy*');

        let BadLabel = GObject.registerClass(class BadLabel extends Gtk.Label {
            vfunc_destroy() {}
        });

        let w = new Gtk.Window();
        w.add(new BadLabel());

        w.destroy();
        System.gc();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGtk.js', 0,
            'Gtk overrides avoid crashing and print a stack trace');
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
});
