#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Gary Li <gary.li1@uwaterloo.ca>

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$TOP_BUILDDIR/gjs-console"
else
    gjs="gjs-console"
fi

total=0

report () {
    exit_code=$?
    total=$((total + 1))
    if test $exit_code -eq 0; then
        echo "ok $total - $1"
    else
        echo "not ok $total - $1"
    fi
}

cat <<'EOF' >gcWrapperWarning.js
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=4.0';

Gtk.init();
const encoder = new TextEncoder();
const Window = GObject.registerClass({
    GTypeName: 'Window',
    Template: encoder.encode(`
        <interface>
            <template class="Window" parent="GtkWindow">
                <property name="child">
                    <object class="GtkListView">
                        <property name="model">
                            <object class="GtkNoSelection">
                                <property name="model" bind-property="model" bind-source="Window"/>
                            </object>
                        </property>
                    </object>
                </property>
            </template>
        </interface>`),
    Properties: {
        'model': GObject.ParamSpec.object('model', '', '',
            GObject.ParamFlags.READWRITE, Gtk.StringList),
    },
}, class Window extends Gtk.Window {
    _init(props = {}) {
        super._init(props);
        this.child.factory = new Gtk.BuilderListItemFactory({bytes: new GLib.Bytes(encoder.encode(`
            <interface>
                <template class="GtkListItem">
                    <property name="child">
                        <object class="Row">
                            <binding name="string-object">
                                <lookup name="item">GtkListItem</lookup>
                            </binding>
                        </object>
                    </property>
                </template>
            </interface>
        `))});
    }
});

const Row = GObject.registerClass({
    GTypeName: 'Row',
    Template: encoder.encode(`
        <interface>
            <template class="Row" parent="GtkBox">
                <child>
                    <object class="GtkLabel">
                        <binding name="label">
                            <lookup name="string" type="GtkStringObject">
                                <lookup name="string-object">Row</lookup>
                            </lookup>
                        </binding>
                    </object>
                </child>
            </template>
        </interface>`),
    Properties: {
        'string-object': GObject.ParamSpec.object('string-object', '', '',
            GObject.ParamFlags.READWRITE, Gtk.StringObject),
    },
}, class Row extends Gtk.Box {
});

const loop = GLib.MainLoop.new(null, false);
const win = new Window({model: Gtk.StringList.new(['test'])});
let weak = new WeakRef(win);
win.connect('close-request', () => loop.quit());
GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
    weak.deref()?.close();
    return false;
});
win.present();
loop.run();
EOF

$gjs -m gcWrapperWarning.js 2>&1 | \
    grep -q 'Wrapper for GObject.*was disposed, cannot set property string-object'
test $? -eq 0
report "Issue 443 GObject wrapper disposed warning"

rm -f gcWrapperWarning.js

echo "1..$total"