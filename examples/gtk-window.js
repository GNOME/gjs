const Gtk = imports.gi.Gtk;

// Initialize Gtk before you start calling anything from the import
Gtk.init(null);

// Construct a top-level window
let window = new Gtk.Window ({
    type: Gtk.WindowType.TOPLEVEL,
    title: "A default title",
    default_width: 300,
    default_height: 250,
    // A decent example of how constants are mapped:
    //     'Gtk' and 'WindowPosition' from the enum name GtkWindowPosition,
    //     'CENTER' from the enum's constant GTK_WIN_POS_CENTER
    window_position: Gtk.WindowPosition.CENTER
});

// Object properties can also be set or changed after construction, unless they
// are marked construct-only.
window.title = "Hello World!";

// This is a callback function
function onDeleteEvent(widget, event, user_data) {
    log("delete-event emitted");
    // If you return false in the "delete_event" signal handler, Gtk will emit
    // the "destroy" signal.
    //
    // Returning true gives you a chance to pop up 'are you sure you want to
    // quit?' type dialogs.
    return false;
};

// When the window is given the "delete_event" signal (this is given by the
// window manager, usually by the "close" option, or on the titlebar), we ask
// it to call the onDeleteEvent() function as defined above.
window.connect("delete-event", onDeleteEvent);

// GJS will warn about unexpected function arguments if you do this...
//
//     window.connect("destroy", Gtk.main_quit);
//
// ...so use arrow functions for inline callbacks with arguments to adjust
window.connect("destroy", (widget, user_data) => {
    Gtk.main_quit();
});

// Create a button to close the window
let button = new Gtk.Button({
    label: "Close the Window",
    // Set visible to 'true' if you don't want to call button.show() later
    visible: true,
    // Another example of constant mapping:
    //     'Gtk' and 'Align' are taken from the GtkAlign enum,
    //     'CENTER' from the constant GTK_ALIGN_CENTER
    valign: Gtk.Align.CENTER,
    halign: Gtk.Align.CENTER
});

// Connect to the 'clicked' signal
button.connect("clicked", (widget, user_data) => {
    window.destroy();
});

// Add the button to the window
window.add(button);

// Show the window
window.show();

// All gtk applications must have a Gtk.main(). Control will end here and wait
// for an event to occur (like a key press or mouse event). The main loop will
// run until Gtk.main_quit is called.
Gtk.main();

