#!/usr/bin/gjs

/*
 * GJS example to show how to render the window buttons.
 * Run it with: gjs metacity.js
 */
const Gdk           = imports.gi.Gdk;
const GLib          = imports.gi.GLib;
const Gtk           = imports.gi.Gtk;
const Lang          = imports.lang;

const Application = new Lang.Class({
    Name: 'Application',

    _init: function() {
        this.title = 'Example of drawing window buttons';
        this._classType = 'close';
        GLib.set_prgname(this.title);
        this.window = null;
        this.application = new Gtk.Application();
        this.application.connect('activate', Lang.bind(this, this._onActivate));
        this.application.connect('startup', Lang.bind(this, this._onStartup));
    },

    run: function () {
        this.application.run([]);
    },

    _onActivate: function () {
        if (this.window) {
            this.window.show_all();
        }
    },

    _onStartup: function () {
        this.buildUI();
    },

    buildUI: function () {
        this.window = new Gtk.ApplicationWindow({
            application: this.application,
            title: this.title,
            default_height: 300,
            default_width: 500,
            window_position: Gtk.WindowPosition.CENTER
        });
        this.window.set_icon_name('application-x-executable');
        this.window.add(this.buildBody());
    },

    buildBody: function () {
        let area = new Gtk.DrawingArea();
        area.set_size_request(250, 300);
        area.connect('draw', (area, ctx) => {
            this.draw(area, ctx); 
        });

        let grid = new Gtk.Grid({ column_spacing: 6, margin: 15, row_spacing: 6 });
        grid.attach(area, 1, 0, 1, 1);

        return grid;
    },

    draw: function(area, cr) {
        // area is Gtk.DrawingArea
        // cr is Cairo.Context
        try {
            let height = area.get_allocated_height();
            let width = area.get_allocated_width();
            let size = Math.min(width, height); 

            // This alternative code will work:
            // let headerWidget = new Gtk.HeaderBar();
            // let buttonWidget = new Gtk.Button();
            // let context = headerWidget.get_style_context();
            // context.add_class('titlebar');
            // headerWidget.add(buttonWidget);
            // context = buttonWidget.get_style_context();
            // context.add_class('titlebutton');
            // context.add_class(this._classType);

            let provider = Gtk.CssProvider.get_default();
            let path = new Gtk.WidgetPath();
            let pos1 = path.append_type(Gtk.HeaderBar);
            let pos2 = path.append_type(Gtk.Button);
            path.iter_add_class(pos1, 'titlebar');
            path.iter_add_class(pos2, 'titlebutton');
            path.iter_add_class(pos2, this._classType);
            let context = new Gtk.StyleContext();
            context.set_screen(Gdk.Screen.get_default());
            context.add_provider (
                provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
            );
            context.set_path(path);

            context.save();
            context.set_state(Gtk.StateFlags.NORMAL);
            Gtk.render_background(context, cr, 0, 0, size, size);
            Gtk.render_frame(context, cr, 0, 0, size, size);
            cr.$dispose();
            context.restore();
        } catch (e) {
            print('Error: ' + e.message);
        }
    },
});

//Run the application
let app = new Application();
app.run(ARGV);

