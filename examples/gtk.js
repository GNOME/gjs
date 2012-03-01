const Gtk = imports.gi.Gtk;

// This is a callback function. The data arguments are ignored
// in this example. More on callbacks below.
function hello(widget) {
    log("Hello World");
}

function onDeleteEvent(widget, event) {
    // If you return FALSE in the "delete_event" signal handler,
    // GTK will emit the "destroy" signal. Returning TRUE means
    // you don't want the window to be destroyed.
    // This is useful for popping up 'are you sure you want to quit?'
    // type dialogs.
    log("delete event occurred");

    // Change FALSE to TRUE and the main window will not be destroyed
    // with a "delete_event".
    return false;
}

function onDestroy(widget) {
    log("destroy signal occurred");
    Gtk.main_quit();
}

Gtk.init(null);

// create a new window
let win = new Gtk.Window({ type: Gtk.WindowType.TOPLEVEL });

// When the window is given the "delete_event" signal (this is given
// by the window manager, usually by the "close" option, or on the
// titlebar), we ask it to call the onDeleteEvent () function
// as defined above.
win.connect("delete-event", onDeleteEvent);

// Here we connect the "destroy" event to a signal handler.
// This event occurs when we call gtk_widget_destroy() on the window,
// or if we return FALSE in the "onDeleteEvent" callback.
win.connect("destroy", onDestroy);

// Sets the border width of the window.
win.set_border_width(10);

// Creates a new button with the label "Hello World".
let button = new Gtk.Button({ label: "Hello World" });

// When the button receives the "clicked" signal, it will call the
// function hello().  The hello() function is defined above.
button.connect("clicked", hello);

// This will cause the window to be destroyed by calling
// gtk_widget_destroy(window) when "clicked". Again, the destroy
// signal could come from here, or the window manager.
button.connect("clicked", function() {
                              win.destroy();
                          });

// This packs the button into the window (a GTK container).
win.add(button);

// The final step is to display this newly created widget.
button.show();

// and the window
win.show();

// All gtk applications must have a Gtk.main(). Control ends here
// and waits for an event to occur (like a key press or mouse event).
Gtk.main();

