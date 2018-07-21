#!/usr/bin/gjs
//The previous line is a hash bang tells how to run the script.
// Note that the script has to be executable (run in terminal in the right folder: chmod +x scriptname)

var Gtk = imports.gi.Gtk;

imports.searchPath.unshift('.');
const WeatherService = imports.geonames;
//Bring your own library from same folder (as set in GJS_PATH). If using autotools .desktop will take care of this

// Initialize the gtk
Gtk.init(null);
//create your window, name it and connect the x to quit function. Remember that window is a taken word
var weatherwindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
weatherwindow.title = 'Todays weather';
//Window only accepts one widget and a title. Further structure with Gtk.boxes of similar
weatherwindow.connect('destroy', function() {
    Gtk.main_quit();
});
//We initialize the icon here, but deside the file later in geonames.js.

var weatherIcon = new Gtk.Image();

//Set some labels to your window
var label1 = new Gtk.Label({label: ''});
var label2 = new Gtk.Label({label: 'Looking in the sky...'});
var label3 = new Gtk.Label({label: ''});

var grid = new Gtk.Grid();
weatherwindow.add(grid);

var entry = new Gtk.Entry();
entry.set_width_chars(4);
entry.set_max_length(4);
entry.text = 'LSZH';
var label4 = new Gtk.Label({label: 'Enter ICAO station for weather: '});
var button1 = new Gtk.Button({label: 'search!'});

//some weather

entry.connect('key_press_event', function() {
    // FIXME: Get weather on enter (key 13)
    if (entry.get_text().length === 4) {
    // Enough is enough
        getWeatherForStation();
    }
    return false;
});

button1.connect('clicked', function() {
    getWeatherForStation();
});

function getWeatherForStation() {
    var station = entry.get_text();

    var GeoNames = new WeatherService.GeoNames(station); //"EFHF";

    GeoNames.getWeather(function(error, weather) {
    //this here works bit like signals. This code will be run when we have weather.
        if (error) {
            label2.set_text('Suggested ICAO station does not exist Try EFHF');
            return; 
        }
        weatherIcon.file = GeoNames.getIcon(weather);

        label1.set_text('Temperature is ' + weather.weatherObservation.temperature + ' degrees.');
        if (weather.weatherObservation.weatherCondition !== 'n/a') {
            label2.set_text('Looks like there is ' + weather.weatherObservation.weatherCondition + ' in the sky.');
        } else {
            label2.set_text('Looks like there is ' + weather.weatherObservation.clouds + ' in the sky.');
        }
        label3.set_text('Windspeed is ' + weather.weatherObservation.windSpeed + ' m/s');
    // ...
    });
}

grid.attach(label4, 2, 1, 1, 1);
grid.attach_next_to(label1, label4, 3, 1, 1);
grid.attach_next_to(label2, label1, 3, 1, 1);
grid.attach_next_to(label3, label2, 3, 1, 1);
grid.attach_next_to(entry, label4, 1, 1, 1);
grid.attach_next_to(button1, entry, 1, 1, 1);
grid.attach_next_to(weatherIcon, label2, 1, 1, 1);
weatherwindow.show_all();
//and run it
Gtk.main();
