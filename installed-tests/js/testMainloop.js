const JSUnit = imports.jsUnit;
const Mainloop = imports.mainloop;

function testTimeout() {
    var trackTimeout = {
        runTenTimes : 0,
        runOnlyOnce: 0,
        neverRun: 0
    };

    Mainloop.timeout_add(10,
                         function() {
                             if (trackTimeout.runTenTimes == 10) {
                                 Mainloop.quit('testtimeout');
                                 return false;
                             }

                             trackTimeout.runTenTimes += 1;
                             return true;
                         });

    Mainloop.timeout_add(10,
                         function () {
                             trackTimeout.runOnlyOnce += 1;
                             return false;
                         });

    Mainloop.timeout_add(15000,
                       function() {
                           trackTimeout.neverRun += 1;
                           return false;
                       });

    Mainloop.run('testtimeout');

    with (trackTimeout) {
        JSUnit.assertEquals("run ten times", 10, runTenTimes);
        JSUnit.assertEquals("run only once", 1, runOnlyOnce);
        JSUnit.assertEquals("never run", 0, neverRun);
    }
}

function testIdle() {
    var trackIdles = {
        runTwiceCount : 0,
        runOnceCount : 0,
        neverRunsCount : 0,
        quitAfterManyRunsCount : 0
    };
    Mainloop.idle_add(function() {
                          trackIdles.runTwiceCount += 1;
                          if (trackIdles.runTwiceCount == 2)
                              return false;
                          else
                              return true;
                      });
    Mainloop.idle_add(function() {
                          trackIdles.runOnceCount += 1;
                          return false;
                      });
    var neverRunsId =
        Mainloop.idle_add(function() {
                              trackIdles.neverRunsCount += 1;
                              return false;
                          });
    Mainloop.idle_add(function() {
                          trackIdles.quitAfterManyRunsCount += 1;
                          if (trackIdles.quitAfterManyRunsCount > 10) {
                              Mainloop.quit('foobar');
                              return false;
                          } else {
                              return true;
                          }
                      });

    Mainloop.source_remove(neverRunsId);

    Mainloop.run('foobar');

    JSUnit.assertEquals("one-shot ran once", 1, trackIdles.runOnceCount);
    JSUnit.assertEquals("two-shot ran twice", 2, trackIdles.runTwiceCount);
    JSUnit.assertEquals("removed never ran", 0, trackIdles.neverRunsCount);
    JSUnit.assertEquals("quit after many ran 11", 11, trackIdles.quitAfterManyRunsCount);

    // check re-entrancy of removing closures while they
    // are being invoked

    trackIdles.removeId = Mainloop.idle_add(function() {
                                                Mainloop.source_remove(trackIdles.removeId);
                                                Mainloop.quit('foobar');
                                                return false;
                                            });
    Mainloop.run('foobar');

    // Add an idle before exit, then never run main loop again.
    // This is to test that we remove idle callbacks when the associated
    // JSContext is blown away. The leak check in gjs-unit will
    // fail if the idle function is not garbage collected.
    Mainloop.idle_add(function() {
                          fail("This should never have been called");
                          return true;
                      });
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);

