# Profiling

## Sysprof

Typical profiling of JavaScript code is performed by passing the `--gjs` and
`--no-perf` options:

```sh
$ sysprof-cli --gjs --no-perf -- gjs script.js
```

This will result in a `capture.syscap` file in the current directory, which can
then be reviewed in the sysprof GUI:

```sh
$ sysprof capture.syscap
```

Other flags can also be combined with `--gjs` when appropriate:

```sh
sysprof-cli --gjs --gtk -- gjs gtk.js
```

#### See Also

* Christian Hergert's [Blog Posts on Sysprof](https://blogs.gnome.org/chergert/category/sysprof/)

