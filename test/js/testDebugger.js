// tests for imports.debugger module

const Debugger = imports.debugger;

function testSetDebugErrorHook() {
    let args;

    let errorHook = function(message, filename, line, pos, flags, errnum, exc) {
        args = { message: message,
                 filename: filename,
                 line: line,
                 pos: pos,
                 flags: flags,
                 errnum: errnum,
                 exc: exc };
    };

    let faulty = function() {
        /* This is a non-obvious way to get an exception raised,
         * just to workaround the error detection in js2-mode.
         * */
        let x = faulty['undefinedProperty'];
    };

    let old = Debugger.setDebugErrorHook(errorHook);
    assertUndefined(old);
    faulty();
    old = Debugger.setDebugErrorHook(null);
    assertEquals(old, errorHook);

    assertNotUndefined(args);
    assertEquals("reference to undefined property faulty.undefinedProperty", args.message);
    assertEquals("args.line", 22, args.line);
    assertEquals("args.pos", 0, args.pos);
    assertEquals("args.flags", 5, args.flags);
    assertEquals("args.errnum", 162, args.errnum);
    assertNotUndefined("args.exc", args.exc);

    assertRaises(function() { Debugger.setDebugErrorHook(); });

}

gjstestRun();
