#!/usr/bin/env gjs

var Gdk = imports.gi.Gdk;
var Gtk = imports.gi.Gtk;
var GLib = imports.gi.GLib;

Gtk.init(null);

var calc_val = "";

function update_display(){
    label.set_markup("<span size='30000'>" + calc_val + "</span>");

    if(calc_val === ""){
	label.set_markup("<span size='30000'>0</span>");
    }
}

function clear(button){
    calc_val = "";
    update_display();
}

function backspace(button){
    calc_val = calc_val.substring(0, calc_val.length - 1);
    update_display();
}

function pressed_equals(button){
    calc_val = calc_val.replace("sin", "Math.sin");
    calc_val = calc_val.replace("cos", "Math.cos");
    calc_val = calc_val.replace("tan", "Math.tan");
    calc_val = eval(calc_val) + "";
    // Avoid rediculous amounts of precision from toString.
    if (calc_val == Math.floor(calc_val))
	calc_val = Math.floor(calc_val);
    else // bizzarely gjs loses str.toFixed() somehow?!
	calc_val = Math.floor(calc_val*10000)/10000;
    label.set_markup("<span size='30000'>" + calc_val + "</span>");
}

function pressed_operator(button){
    calc_val += button.label;
    update_display();
}

function pressed_number(button){
    calc_val = (((calc_val === 0) ? "" : calc_val) + button.label);
    update_display();
}

function swap_sign(button){
    calc_val = ((calc_val[0] == "-") ?
		calc_val.substring(1) : "-" + calc_val);
    update_display();
}

function random_num(){
    calc_val = Math.floor(Math.random() * 1000) + "";
    update_display();
}

function pack_buttons(buttons, vbox){
    var hbox = new Gtk.HBox();

    hbox.homogeneous = true;

    vbox.pack_start(hbox, true, true, 2);

    for(let i = 0; i <= 4; i++){
	hbox.pack_start(buttons[i], true, true, 1);
    }
}

function create_button(str, func){
    var btn = new Gtk.Button({label:str});
    btn.connect("clicked",func);
    return btn;
}

function create_buttons(){
    var vbox = new Gtk.VBox();

    vbox.homogeneous = true;

    pack_buttons([	create_button("(", pressed_number),
			create_button("←", backspace),
			create_button("↻", random_num),
			create_button("Clr", clear),
			create_button("±", swap_sign)], vbox);


    pack_buttons([	create_button(")", pressed_number),
			create_button("7", pressed_number),
			create_button("8", pressed_number),
			create_button("9", pressed_number),
			create_button("/", pressed_operator)], vbox);

    pack_buttons([	create_button("sin(", pressed_number),
			create_button("4", pressed_number),
			create_button("5", pressed_number),
			create_button("6", pressed_number),
			create_button("*", pressed_operator)], vbox);

    pack_buttons([	create_button("cos(", pressed_number),
			create_button("1", pressed_number),
			create_button("2", pressed_number),
			create_button("3", pressed_number),
			create_button("-", pressed_operator)], vbox);

    pack_buttons([	create_button("tan(", pressed_number),
			create_button("0", pressed_number),
			create_button(".", pressed_number),
			create_button("=", pressed_equals),
			create_button("+", pressed_operator)], vbox);

    return vbox;
}

var window = new Gtk.Window({title: "Calculator", resizable: false});

window.resize(250, 250);
window.connect("destroy", () => {
    Gtk.main_quit();
});

window.opacity = 0.6;

var label = new Gtk.Label({label: ""});
label.set_alignment(1,0);
update_display();

var mainvbox = new Gtk.VBox();
mainvbox.pack_start(label, false, true, 1);
mainvbox.pack_start(new Gtk.HSeparator(), false, true, 5);
mainvbox.pack_start(create_buttons(), true, true, 2);

window.add(mainvbox);
window.show_all();
Gtk.main();
