imports.gi.versions.Clutter = '0.8';
const Clutter = imports.gi.Clutter;

Clutter.init(null, null);

let stage = new Clutter.Stage();

let texture = new Clutter.Texture({ filename: 'test.jpg',
                                    reactive: true });

texture.connect('button-press-event',
                function(o, event) {
                    log('Clicked!');
                    return true;
                });

let color = new Clutter.Color();
Clutter.color_parse('Black', color);

stage.color = color;

stage.add_actor(texture);
stage.show();

Clutter.main();
