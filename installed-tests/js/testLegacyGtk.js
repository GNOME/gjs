// -*- mode: js; indent-tabs-mode: nil -*-
/* eslint-disable no-restricted-properties */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Giovanni Campagna <gcampagna@src.gnome.org>

imports.gi.versions.Gtk = '3.0';

const {GLib, Gtk} = imports.gi;
const Lang = imports.lang;
const System = imports.system;

const template = `
<interface>
  <template class="Gjs_MyComplexGtkSubclass" parent="GtkGrid">
    <property name="margin_top">10</property>
    <property name="margin_bottom">10</property>
    <property name="margin_start">10</property>
    <property name="margin_end">10</property>
    <property name="visible">True</property>
    <child>
      <object class="GtkLabel" id="label-child">
        <property name="label">Complex!</property>
        <property name="visible">True</property>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="label-child2">
        <property name="label">Complex as well!</property>
        <property name="visible">True</property>
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

const MyComplexGtkSubclass = new Lang.Class({
    Name: 'MyComplexGtkSubclass',
    Extends: Gtk.Grid,
    Template: new TextEncoder().encode(template),
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],
    CssName: 'complex-subclass',

    testChildrenExist() {
        this._internalLabel = this.get_template_child(MyComplexGtkSubclass, 'label-child');
        expect(this._internalLabel).toEqual(jasmine.anything());

        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    },
});

const MyComplexGtkSubclassFromResource = new Lang.Class({
    Name: 'MyComplexGtkSubclassFromResource',
    Extends: Gtk.Grid,
    Template: 'resource:///org/gjs/jsunit/complex3.ui',
    Children: ['label-child', 'label-child2'],
    InternalChildren: ['internal-label-child'],

    testChildrenExist() {
        expect(this.label_child).toEqual(jasmine.anything());
        expect(this.label_child2).toEqual(jasmine.anything());
        expect(this._internal_label_child).toEqual(jasmine.anything());
    },

    templateCallback() {},
    boundCallback() {},
});

function validateTemplate(description, ClassName) {
    describe(description, function () {
        let win, content;
        beforeEach(function () {
            win = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
            content = new ClassName();
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

        afterEach(function () {
            win.destroy();
        });
    });
}

describe('Legacy Gtk overrides', function () {
    beforeAll(function () {
        Gtk.init(null);
    });

    validateTemplate('UI template', MyComplexGtkSubclass);
    validateTemplate('UI template from resource', MyComplexGtkSubclassFromResource);

    it('sets CSS names on classes', function () {
        expect(Gtk.Widget.get_css_name.call(MyComplexGtkSubclass)).toEqual('complex-subclass');
    });

    function asyncIdle() {
        return new Promise(resolve => {
            GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
                resolve();
                return GLib.SOURCE_REMOVE;
            });
        });
    }

    it('does not leak instance when connecting template signal', async function () {
        const LeakTestWidget = new Lang.Class({
            Name: 'LeakTestWidget',
            Extends: Gtk.Button,
            Template: new TextEncoder().encode(`
                <interface>
                    <template class="Gjs_LeakTestWidget" parent="GtkButton">
                        <signal name="clicked" handler="buttonClicked"/>
                    </template>
                </interface>`),

            buttonClicked() {},
        });

        const weakRef = new WeakRef(new LeakTestWidget());

        await asyncIdle();
        // It takes two GC cycles to free the widget, because of the tardy sweep
        // problem (https://gitlab.gnome.org/GNOME/gjs/-/issues/217)
        System.gc();
        System.gc();

        expect(weakRef.deref()).toBeUndefined();
    });
});
