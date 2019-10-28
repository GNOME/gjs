#!/usr/bin/env -S gjs -m

import Gtk from "gi://Gtk?v=3.0"
import system from "system"
export class Application {

    message = "Hello World";
    application = new Gtk.Application();

    //create the application
    constructor() {
        //connect to 'activate' and 'startup' signals to handlers.
        this.application.connect('activate', this._onActivate.bind(this));
        this.application.connect('startup', this._onStartup.bind(this));
    }

    //create the UI
    _buildUI() {
        this._window = new Gtk.ApplicationWindow({
            application: this.application,
            title: "Hello World!"
        });
        this._window.set_default_size(200, 200);

        this.label = new Gtk.Label({ label: "Hello World" });
        this.btn = new Gtk.Button({ label: "Quit" });
        this.hello_btn = new Gtk.Button({ label: "Hello World" });

        this.box = new Gtk.Box();
        this.box.add(this.label);
        this.box.add(this.btn);
        this.box.add(this.hello_btn);

        this._window.add(this.box);

        this.btn.connect('clicked', () => {
            this.application.quit();
            console.log(Object.getOwnPropertyNames(system));
            system.exit(1);
        });

        this.hello_btn.connect('clicked', () => {
            console.log('clicked');
        })
    }

    //handler for 'activate' signal
    _onActivate() {
        //show the window and all child widgets
        this._window.show_all();
    }

    //handler for 'startup' signal
    _onStartup() {
        this._buildUI();
    }
};

//run the application
let app = new Application();

console.log(JSON.stringify(ARGV));

app.application.run(ARGV);