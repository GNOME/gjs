# Cairo

**Import with `import Cairo from 'cairo';`**

Mostly API compatible with [cairo](https://www.cairographics.org/documentation/), but using camelCase function names. There is list of constants in [cairo.js][cairo-const] and functions for each object in its corresponding C++ file (eg. [cairo-context.cpp][cairo-func]). A simple example drawing a 32x32 red circle:

```js
import Gtk from 'gi://Gtk?version=3.0';
import Cairo from 'cairo';

let drawingArea = new Gtk.DrawingArea({
    height_request: 32,
    width_request: 32
});

drawingArea.connect("draw", (widget, cr) => {
    // Cairo in GJS uses camelCase function names
    cr.setSourceRGB(1.0, 0.0, 0.0);
    cr.setOperator(Cairo.Operator.DEST_OVER);
    cr.arc(16, 16, 16, 0, 2*Math.PI);
    cr.fill();
    // currently when you connect to a draw signal you have to call
    // cr.$dispose() on the Cairo context or the memory will be leaked.
    cr.$dispose();
    return false;
});
```

[cairo-const]: https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/cairo.js
[cairo-func]: https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/cairo-context.cpp#L825
