// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Andy Holmes <andyholmes@gnome.org>


import Gtk from 'gi://Gtk?version=4.0';
import GLib from 'gi://GLib';


function easeInOutQuad(p) {
    if ((p *= 2.0) < 1.0)
        return 0.5 * p * p;

    return -0.5 * (--p * (p - 2) - 1);
}

// When the button is clicked, we'll add a tick callback to run the animation
function _onButtonClicked(widget) {
    // Prevent concurrent animations from being triggered
    if (widget._animationId)
        return;

    const duration = 1000; // one second in milliseconds
    let start = Date.now();
    let fadeIn = false;

    // Tick callbacks are just like GSource callbacks. You will get a ID that
    // can be passed to Gtk.Widget.remove_tick_callback(), or you can return
    // GLib.SOURCE_CONTINUE and GLib.SOURCE_REMOVE as appropriate.
    widget._animationId = widget.add_tick_callback(() => {
        let now = Date.now();

        // We've now passed the time duration
        if (now >= start + duration) {
            // If we just finished fading in, we're all done
            if (fadeIn) {
                widget._animationId = null;
                return GLib.SOURCE_REMOVE;
            }

            // If we just finished fading out, we'll start fading in
            fadeIn = true;
            start = now;
        }

        // Apply the easing function to the current progress
        let progress = (now - start) / duration;
        progress = easeInOutQuad(progress);

        // We are using the progress as the opacity value of the button
        widget.opacity = fadeIn ? progress : 1.0 - progress;

        return GLib.SOURCE_CONTINUE;
    });
}


// Initialize GTK
Gtk.init();
const loop = GLib.MainLoop.new(null, false);

// Create a button to start the animation
const button = new Gtk.Button({
    label: 'Fade out, fade in',
    valign: Gtk.Align.CENTER,
    halign: Gtk.Align.CENTER,
});
button.connect('clicked', _onButtonClicked);

// Create a top-level window
const win = new Gtk.Window({
    title: 'GTK4 Frame Clock',
    default_width: 300,
    default_height: 250,
    child: button,
});

// When a widget is destroyed any tick callbacks will be removed automatically,
// so in practice our callback would be cleaned up when the window closes.
win.connect('close-request', () => {
    // Note that removing a tick callback by ID will interrupt its progress, so
    // we are resetting the button opacity manually after it's removed.
    if (button._animationId) {
        button.remove_tick_callback(button._animationId);
        button.opacity = 1.0;
    }

    loop.quit();
});

// Show the window
win.present();
loop.run();

