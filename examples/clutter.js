const Clutter = imports.gi.clutter;

Clutter.init(null, null);

let stage = new Clutter.Stage();

let texture = new Clutter.Texture({ filename: '' });

stage.add_actor(texture);
stage.show();

Clutter.main();
