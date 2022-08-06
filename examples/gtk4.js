// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

// Include the version in case both GTK3 and GTK4 installed
// otherwise an exception will be thrown
import Gtk from 'gi://Gtk?version=4.0';
import GLib from 'gi://GLib';

// Initialize Gtk before you start calling anything from the import
Gtk.init();

// If you are not using GtkApplication which has its own mainloop
// you must create it yourself, see gtk-application.js example
let loop = GLib.MainLoop.new(null, false);

// Construct a window
let win = new Gtk.Window({
    title: 'A default title',
    default_width: 300,
    default_height: 250,
});

// Object properties can also be set or changed after construction, unless they
// are marked construct-only.
win.title = 'Hello World!';

// This is a callback function
function onCloseRequest() {
    log('close-request emitted');
    loop.quit();
}

// When the window is given the "close-request" signal (this is given by the
// window manager, usually by the "close" option, or on the titlebar), we ask
// it to call the onCloseRequest() function as defined above.
win.connect('close-request', onCloseRequest);

// Create a button to close the window
let button = new Gtk.Button({
    label: 'Close the Window',
    // An example of how constants are mapped:
    //     'Gtk' and 'Align' are taken from the GtkAlign enum,
    //     'CENTER' from the constant GTK_ALIGN_CENTER
    valign: Gtk.Align.CENTER,
    halign: Gtk.Align.CENTER,
});

// Connect to the 'clicked' signal, using another way to call an arrow function
button.connect('clicked', () => win.close());

// Add the button to the window
win.set_child(button);

// Show the window
win.present();

// Control will end here and wait for an event to occur
// (like a key press or mouse event)
// The main loop will run until loop.quit is called.
loop.run();

log('The main loop has completed.');
