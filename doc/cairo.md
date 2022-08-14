# Cairo

The `Cairo` module is a set of custom bindings for the [cairo][cairo] 2D
graphics library. Cairo is used by GTK, Clutter, Mutter and others for drawing
shapes, text, compositing images and performing affine transformations.

The GJS bindings for cairo follow the C API pretty closely, although some of the
less common functions are not available yet. In spite of this, the bindings are
complete enough that the upstream [cairo documentation][cairo-docs] may be
helpful to those new to using Cairo.

[cairo]: https://www.cairographics.org/
[cairo-docs]: https://www.cairographics.org/documentation/

#### Import

When using ESModules:

```js
import Cairo from 'cairo';
```

When using legacy imports:

```js
const Cairo = imports.cairo;
```

#### Mapping

Methods are studlyCaps, similar to other JavaScript APIs. Abbreviations such as
RGB, RGBA, PNG, PDF and SVG are always upper-case. For example:

* `cairo_move_to()` is mapped to `Cairo.Context.moveTo()`
* `cairo_surface_write_to_png()` is mapped to `Cairo.Context.writeToPNG()`

Unlike the methods and structures, Cairo's enumerations are documented
alongside the other GNOME APIs in the [`cairo`][cairo-devdocs] namespace. These
are mapped similar to other libraries in GJS (eg. `Cairo.Format.ARGB32`).

[cairo-devdocs]: https://gjs-docs.gnome.org/cairo10

## Cairo.Context (`cairo_t`)

`cairo_t` is mapped as `Cairo.Context`.

You will either get a context from a third-party library such
as Clutter/Gtk/Poppler or by calling the `Cairo.Context` constructor.

```js
let cr = new Cairo.Context(surface);

let cr = Gdk.cairo_create(...);
```

All introspection methods taking or returning a `cairo_t` will automatically
create a `Cairo.Context`.

### Cairo.Context.$dispose()

> Attention: This method must be called to avoid leaking memory

Free a `Cairo.Context` and all associated memory.

Unlike other objects and values in GJS, the `Cairo.Context` object requires an
explicit free function to avoid memory leaks. However you acquire a instance,
the `Cairo.Context.$dispose()` method must be called when you are done with it.

For example, when using a [`Gtk.DrawingArea`][gtkdrawingarea]:

```js
import Cairo from 'cairo';
import Gtk from 'gi://Gtk?version=4.0';

// Initialize GTK
Gtk.init();

// Create a drawing area and set a drawing function
const drawingArea = new Gtk.DrawingArea();

drawingArea.set_draw_func((area, cr, width, height) => {
    // Perform operations on the surface context

    // Freeing the context before returning from the callback
    cr.$dispose();
});
```

[gtkdrawingarea]: https://gjs-docs.gnome.org/gtk40/gtk.drawingarea


## Cairo.Pattern (`cairo_pattern_t`)

Prototype hierarchy

* `Cairo.Pattern`
  * `Cairo.Gradient`
    * `Cairo.LinearGradient`
    * `Cairo.RadialGradient`
  * `Cairo.SurfacePattern`
  * `Cairo.SolidPattern`

You can create a linear gradient by calling the constructor:

Constructors:
```js
let pattern = new Cairo.LinearGradient(0, 0, 100, 100);

let pattern = new Cairo.RadialGradient(0, 0, 10, 100, 100, 10);

let pattern = new Cairo.SurfacePattern(surface);

let pattern = new Cairo.SolidPattern.createRGB(0, 0, 0);

let pattern = new Cairo.SolidPattern.createRGBA(0, 0, 0, 0);
```

## Cairo.Surface (`cairo_surface_t`)

Prototype hierarchy

* `Cairo.Surface` (abstract)
  * `Cairo.ImageSurface`
  * `Cairo.PDFSurface`
  * `Cairo.SVGSurface`
  * `Cairo.PostScriptSurface`

The native surfaces (win32, quartz, xlib) are not supported at this time.

Methods manipulating a surface are present in the surface class. For example,
creating a `Cairo.ImageSurface` from a PNG is done by calling a static method:

```js
const surface = Cairo.ImageSurface.createFromPNG('filename.png');
```

## To-do List

As previously mentioned, the Cairo bindings for GJS are not entirely complete
and contributions are welcome. Some of the bindings left to be implemented
include:

* context: wrap the remaining methods
* surface methods
* image surface methods
* matrix
* version
* iterating over `cairo_path_t`

Many font and glyph operations are not yet supported, and it is recommended to
use [`PangoCairo`][pango-cairo] as an alternative:

* glyphs
* text cluster
* font face
* scaled font
* font options

[pango-cairo]: https://gjs-docs.gnome.org/pangocairo10
