# Environment

GJS allows runtime configuration with a number of environment variables.

## General

* `GJS_PATH`

  Set this variable to a list of colon-separated (`:`) paths (just like `PATH`),
  to add them to the search path for the importer. Use of the `--include-path`
  command-line option is preferred over this variable.

* `GJS_ABORT_ON_OOM`

  > NOTE: This feature is not well tested.

  Setting this variable to any value causes GJS to exit when an out-of-memory
  condition is encountered, instead of just printing a warning.

* `GJS_REPL_HISTORY`

  When not set, GJS persists REPL history in `gjs_repl_history` under the XDG user cache folder which is usually `~/.cache/`. Set this variable to a writable path to save REPL command history in an alternate location. If set to an empty string, then command history is not persisted.

## JavaScript Engine

* `JS_GC_ZEAL`

  Enable GC zeal, a testing and debugging feature that helps find GC-related
  bugs in JSAPI applications. See the [Hacking][hacking-gczeal] and the
  [JSAPI Documentation][mdn-gczeal] for more information about this variable.

* `GJS_DISABLE_JIT`

  Setting this variable to any value will disable JIT compiling in the
  JavaScript engine.


## Debugging

* `GJS_DEBUG_HEAP_OUTPUT`

  In addition to `System.dumpHeap()`, you can dump a heap from a running program
  by starting it with this environment variable set to a path and sending it the
  `SIGUSR1` signal.

* `GJS_DEBUG_OUTPUT`

  Set this to "stderr" to log to `stderr` or a file path to save to.

* `GJS_DEBUG_TOPICS`

  Set this to a semi-colon delimited (`;`) list of prefixes to allow to be
  logged. Prefixes include:

   * "JS GI USE"
   * "JS MEMORY"
   * "JS CTX"
   * "JS IMPORT"
   * "JS NATIVE"
   * "JS KP ALV"
   * "JS G REPO"
   * "JS G NS"
   * "JS G OBJ"
   * "JS G FUNC"
   * "JS G FNDMTL"
   * "JS G CLSR"
   * "JS G BXD"
   * "JS G ENUM"
   * "JS G PRM"
   * "JS G ERR"
   * "JS G IFACE"

* `GJS_DEBUG_THREAD`

  Set this variable to print the thread number when logging.

* `GJS_DEBUG_TIMESTAMP`

  Set this variable to print a timestamp when logging.


## Testing

* `GJS_COVERAGE_OUTPUT`

  Set this variable to define an output path for code coverage information. Use
  of the `--coverage-output` command-line option is preferred over this
  variable.

* `GJS_COVERAGE_PREFIXES`

  Set this variable to define a colon-separated (`:`) list of prefixes to output
  code coverage information for. Use of the `--coverage-prefix` command-line
  option is preferred over this variable.

* `GJS_ENABLE_PROFILER`

  Set this variable to `1` to enable or `0` to disable the profiler. Use of the
  `--profile` command-line option is preferred over this variable.

* `GJS_TRACE_FD`

  The GJS profiler is integrated directly into Sysprof via this variable. It not
  typically useful to set this manually.


[hacking-gczeal]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/doc/Hacking.md#gc-zeal
[mdn-gczeal]: https://developer.mozilla.org/docs/Mozilla/Projects/SpiderMonkey/JSAPI_reference/JS_SetGCZeal
