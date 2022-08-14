# Testing

Testing infrastructure for GJS code is unfortunately not as complete as other
languages, and help in the area would be a greatly appreciated contribution to
the community.

## Jasmine GJS

[Jasmine GJS][jasmine-gjs] is a fork of the Jasmine testing framework, adapted
for GJS and the GLib event loop. See the [Jasmine Documentation][jasmine-doc]
and the [GJS test suite][gjs-tests] for examples.

[jasmine-doc]: https://jasmine.github.io/pages/docs_home.html
[jasmine-gjs]: https://github.com/ptomato/jasmine-gjs
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/installed-tests/js

## jsUnit

> Deprecated: Use [Jasmine GJS](#jasmine-gjs) instead

The `jsUnit` module was originally used as the testing framework in GJS. It has
long been deprecated in favour of Jasmine.

