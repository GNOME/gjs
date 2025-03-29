// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Andy Holmes <andyholmes@gnome.org>

import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import Gtk from 'gi://Gtk?version=4.0';

Gtk.init();


/* In this example the template contents are loaded from the file as a string.
 *
 * The `Template` property of the class definition will accept:
 *   - a `Uint8Array` or `GLib.Bytes` of XML
 *   - an absolute file URI, such as `file:///home/user/window.ui`
 *   - a GResource URI, such as `resource:///org/gnome/AppName/window.ui`
 */
const file = Gio.File.new_for_uri(import.meta.url);
const templateFile = file.get_parent().resolve_relative_path('gtk4-template.ui');
const [, template] = templateFile.load_contents(null);


const ExampleWindow = GObject.registerClass({
    GTypeName: 'ExampleWindow',
    Template: template,
    Children: [
        'box',
    ],
    InternalChildren: [
        'button',
    ],
}, class ExampleWindow extends Gtk.Window {
    constructor(params = {}) {
        super(params);

        // The template has been initialized and you can access the children
        this.box.visible = true;

        // Internal children are set on the instance prefixed with a `_`
        this._button.visible = true;
    }

    // The signal handler bound in the UI file
    _onButtonClicked(button) {
        if (this instanceof Gtk.Window)
            log('Callback scope is bound to `ExampleWindow`');

        button.label = 'Button was clicked!';
    }
});


// Create a window that stops the program when it is closed
const loop = GLib.MainLoop.new(null, false);

const win = new ExampleWindow();
win.connect('close-request', () => loop.quit());
win.present();

loop.run();

