const Clutter = imports.gi.Clutter;

Clutter.init(null);

const stage = new Clutter.Stage({visible: true});

let texture = new Clutter.Texture({
    filename: 'test.jpg',
    reactive: true,
});

texture.connect('button-press-event', () => {
    log('Clicked!');
    return Clutter.EVENT_STOP;
});

const [, color] = Clutter.Color.from_string('Black');
stage.background_color = color;

stage.add_child(texture);

Clutter.main();
