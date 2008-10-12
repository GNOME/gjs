const Clutter = imports.gi.clutter;

Clutter.init(null, null);

let stage = new Clutter.Stage();

let texture = new Clutter.Texture({ filename: 'test.jpg',
                                    reactive: true });

texture.connect('button-press-event',
                function(o, event) {
                    log('Clicked!');
                    return true;
                });

stage.add_actor(texture);
stage.show();

Clutter.main();
