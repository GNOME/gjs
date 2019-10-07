const Clutter = imports.gi.Clutter;

Clutter.init(null);

let stage = new Clutter.Stage();

let texture = new Clutter.Texture({
    filename: 'test.jpg',
    reactive: true,
});

texture.connect('button-press-event', () => {
    log('Clicked!');
    return Clutter.EVENT_STOP;
});

let [,color] = Clutter.Color.from_string('black');

stage.color = color;

stage.add_actor(texture);
stage.show();

Clutter.main();
