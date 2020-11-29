The cairo bindings follows the C API pretty closely.

## Naming ##

The module name is called 'cairo' and usually imported into
the namespace as 'Cairo'.
```js
const Cairo = imports.cairo;
```
Methods are studlyCaps, similar to other JavaScript apis, eg

`cairo_move_to` is wrapped to `Cairo.Context.moveTo()`
`cairo_surface_write_to_png` to `Cairo.Context.writeToPNG()`.

Abbreviations such as RGB, RGBA, PNG, PDF, SVG are always
upper-case.

Enums are set in the cairo namespace, the enum names are capitalized:

`CAIRO_FORMAT_ARGB32` is mapped to `Cairo.Format.ARGB32` etc.

## Surfaces (`cairo_surface_t`) ##

Prototype hierarchy

* `Surface` (abstract)
  * `ImageSurface`
  * `PDFSurface`
  * `SVGSurface`
  * `PostScriptSurface`

The native surfaces (win32, quartz, xlib) are not supported at this point.

Methods manipulating a surface are present in the surface class.
Creating an ImageSurface from a PNG is done by calling a static method:
```js
let surface = Cairo.ImageSurface.createFromPNG("filename.png");
```

## Context (`cairo_t`) ##

`cairo_t` is mapped as `Cairo.Context`.

You will either get a context from a third-party library such
as Clutter/Gtk/Poppler or by calling the `Cairo.Context` constructor.

```js
let cr = new Cairo.Context(surface);

let cr = Gdk.cairo_create(...);
```
All introspection methods taking or returning a `cairo_t` will automatically
create a `Cairo.Context`.

## Patterns (`cairo_pattern_t`) ##

Prototype hierarchy

* `Pattern`
  * `Gradient`
    * `LinearGradient`
    * `RadialGradient`
  * `SurfacePattern`
  * `SolidPattern`

You can create a linear gradient by calling the constructor:

Constructors:
```js
let pattern = new Cairo.LinearGradient(0, 0, 100, 100);

let pattern = new Cairo.RadialGradient(0, 0, 10, 100, 100, 10);

let pattern = new Cairo.SurfacePattern(surface);

let pattern = new Cairo.SolidPattern.createRGB(0, 0, 0);

let pattern = new Cairo.SolidPattern.createRGBA(0, 0, 0, 0);
```
TODO:
* context: wrap the remaining methods
* surface methods
* image surface methods
* matrix
* version
* iterating over `cairo_path_t`

Fonts & Glyphs are not wrapped, use PangoCairo instead.
* glyphs
* text cluster
* font face
* scaled font
* font options
