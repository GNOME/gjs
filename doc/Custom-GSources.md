## Custom GSources

GLib allows custom GSources to be added to the main loop.
A custom GSource can control under what conditions it is dispatched.
You can read more about GLib's main loop [here][glib-mainloop-docs].

Within GJS, we have implemented a custom GSource to handle Promise execution.
It dispatches whenever a Promise is queued, occurring before any other GLib
events.
This mimics the behavior of a [microtask queue](mdn-microtasks) in other
JavaScript environments.
You can read an introduction to building custom GSources within the archived
developer documentation [here][custom-gsource-tutorial-source].
Another great resource is Philip Withnall's ["A detailed look at GSource"][gsource-blog-post]<sup>[[permalink]][gsource-blog-post-archive]</sup>.

[gsource-blog-post]: https://tecnocode.co.uk/2015/05/05/a-detailed-look-at-gsource/
[gsource-blog-post-archive]: https://web.archive.org/web/20201013000618/https://tecnocode.co.uk/2015/05/05/a-detailed-look-at-gsource/
[mdn-microtasks]: https://developer.mozilla.org/en-US/docs/Web/API/HTML_DOM_API/Microtask_guide
[glib-mainloop-docs]: https://docs.gtk.org/glib/main-loop.html#creating-new-source-types
[custom-gsource-tutorial-source]: https://gitlab.gnome.org/Archive/gnome-devel-docs/-/blob/703816cec292293fd337b6db8520b9b0afa7b3c9/platform-demos/C/custom-gsource.c.page
