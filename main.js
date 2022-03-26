import Gtk from 'gi://Gtk';
Gtk.init();

log('Initializing...');

const worker = new Worker('./worker.js', {name: '"My New Friend"'});

let value = 0;

const window = new Gtk.Window();
const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 20, marginTop: 10, marginBottom: 10, marginStart: 10, marginEnd: 10, });
const label = new Gtk.Label({ label: `Value: ${value}` });
const button = new Gtk.Button({ label: 'Add 10' });
box.append(label);
box.append(button);
window.set_child(box);

button.connect('clicked', () => {
    worker.postMessage(value);
});

worker.onmessage = (event) => {
    console.log(`Why hello there my worker thread, I got ${event.data} from you and I'm updating the value.`);
    value = event.data;
    label.label = `Value: ${value}`;
}
window.present();

imports.mainloop.run();