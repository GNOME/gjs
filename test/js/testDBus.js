// application/javascript;version=1.8
const DBus = imports.dbus;
const Mainloop = imports.mainloop;

/* Malarky is the proxy object */
function Malarky() {
    this._init();
}

Malarky.prototype = {
    _init: function() {
        DBus.session.proxifyObject(this, 'com.litl.Real', '/com/litl/Real');
    }
};

/* The methods list with their signatures. We test both notations for json
 * methods (only name or explicit signatures)
 *
 * *** NOTE: If you add stuff here, you need to update testIntrospectReal
 */
var realIface = {
    name: 'com.litl.Real',
    methods: [{ name: 'nonJsonFrobateStuff',
                outSignature: 's', inSignature: 'i' },
              { name: 'frobateStuff' },
              { name: 'alwaysThrowException',
                outSignature: 'a{sv}', inSignature: 'a{sv}' },
              { name: 'thisDoesNotExist' },
              { name: 'noInParameter',
                outSignature: 's', inSignature: '' },
              { name: 'multipleInArgs',
                outSignature: 's', inSignature: 'iiiii' },
              { name: 'noReturnValue',
                outSignature: '', inSignature: '' },
              { name: 'emitSignal',
                outSignature: '', inSignature: '' },
              { name: "multipleOutValues", outSignature: "sss",
                inSignature: "" },
              { name: "oneArrayOut", outSignature: "as",
                inSignature: "" },
              { name: "arrayOfArrayOut", outSignature: "aas",
                inSignature: "" },
              { name: "multipleArrayOut", outSignature: "asas",
                inSignature: "" },
              { name: "arrayOutBadSig", outSignature: "i",
                inSignature: "" },
              { name: "changingSig", outSignature: "",
                inSignature: "b" },
              { name: "byteArrayEcho", outSignature: "ay",
                inSignature: "ay" },
              { name: "byteEcho", outSignature: "y",
                inSignature: "y" },
              { name: "dictEcho", outSignature: "a{sv}",
                inSignature: "a{sv}" },
              { name: "echo", outSignature: "si",
                inSignature: "si" },
              { name: "structArray", outSignature: "a(ii)",
                inSignature: '' }
             ],
    signals: [
        { name: 'signalFoo', inSignature: 's' }
    ],
    properties: [
        { name: 'PropReadOnly', signature: 'b', access: 'read' },
        { name: 'PropWriteOnly', signature: 's', access: 'write' },
        { name: 'PropReadWrite', signature: 'v', access: 'readwrite' }
    ]
};

DBus.proxifyPrototype(Malarky.prototype,
                      realIface);

/* Real is the actual object exporting the dbus methods */
function Real() {
    this._init();
}

const PROP_READ_WRITE_INITIAL_VALUE = 58;
const PROP_WRITE_ONLY_INITIAL_VALUE = "Initial value";

Real.prototype = {
    _init: function(){
        this._propWriteOnly = PROP_WRITE_ONLY_INITIAL_VALUE;
        this._propReadWrite = PROP_READ_WRITE_INITIAL_VALUE;
    },

    frobateStuff: function(args) {
        return { hello: 'world' };
    },

    nonJsonFrobateStuff: function(i) {
        if (i == 42) {
            return "42 it is!";
        } else {
            return "Oops";
        }
    },

    alwaysThrowException: function() {
        throw "Exception!";
    },

    thisDoesNotExist: function () {
        /* We'll remove this later! */
    },

    noInParameter: function() {
        return "Yes!";
    },

    multipleInArgs: function(a, b, c, d, e) {
        return a + " " + b + " " + c + " " + d + " " + e;
    },

    emitSignal: function() {
        DBus.session.emit_signal('/com/litl/Real', 'com.litl.Real', 'signalFoo', 's', [ "foobar" ]);
    },

    noReturnValue: function() {
        /* Empty! */
    },

    /* The following two functions have identical return values
     * in JS, but the bus message will be different.
     * multipleOutValues is "sss", while oneArrayOut is "as"
     */
    multipleOutValues: function() {
        return [ "Hello", "World", "!" ];
    },

    oneArrayOut: function() {
        return [ "Hello", "World", "!" ];
    },

    /* Same thing again. In this case multipleArrayOut is "asas",
     * while arrayOfArrayOut is "aas".
     */
    multipleArrayOut: function() {
        return [[ "Hello", "World" ], [ "World", "Hello" ]];
    },

    arrayOfArrayOut: function() {
        return [[ "Hello", "World" ], [ "World", "Hello" ]];
    },

    arrayOutBadSig: function() {
        return [ "Hello", "World", "!" ];
    },

    /* This is kinda scary, but we do it in getKeyForNetwork.
     * Basically you can change the outSignature at runtime
     * to have variable return value (in number and type) methods.
     */
    changingSig: function(b) {
        if (b == true) {
            arguments.callee.outSignature = "s";
            return "CHANGE";
        } else {
            arguments.callee.outSignature = "i";
            return 42;
        }
    },

    byteArrayEcho: function(binaryString) {
        return binaryString;
    },

    byteEcho: function(aByte) {
        return aByte;
    },

    dictEcho: function(dict) {
        return dict;
    },

    /* This one is implemented asynchronously. Returns
     * the input arguments */
    echoAsync: function(someString, someInt, callback) {
        Mainloop.idle_add(function() {
                              callback([someString, someInt]);
                              return false;
                          });
    },

    // boolean
    get PropReadOnly() {
        return true;
    },

    // string
    set PropWriteOnly(value) {
        this._propWriteOnly = value;
    },

    // variant
    get PropReadWrite() {
        return this._propReadWrite;
    },

    set PropReadWrite(value) {
        this._propReadWrite = value;
    },

    structArray: function () {
        return [[128, 123456], [42, 654321]];
    }
};

DBus.conformExport(Real.prototype, realIface);
DBus.session.exportObject('/com/litl/Real', new Real());
DBus.session.acquire_name('com.litl.Real', DBus.SINGLE_INSTANCE,
                          function(name){log("Acquired name " + name);},
                          function(name){log("Lost name  " + name);});

function testFrobateStuff() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.frobateStuffRemote({},
                                                   function(result, excp) {
                                                       theResult = result;
                                                       theExcp = excp;
                                                       Mainloop.quit('testDbus');
                                                   });
                      });

    Mainloop.run('testDbus');
    assertEquals("world", theResult.hello);
}

/* excp must be exactly the exception thrown by the remote method */
function testThrowException() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.alwaysThrowExceptionRemote({},
                                                           function(result, excp) {
                                                               theResult = result;
                                                               theExcp = excp;
                                                               Mainloop.quit('testDbus');
                                                           });
                      });

    Mainloop.run('testDbus');

    assertNotNull(theExcp);
}

/* We check that the exception in the answer is not null when we try to call
 * a method that does not exist */
function testDoesNotExist() {
    let theResult, theExcp;

    /* First remove the method from the object! */
    delete Real.prototype.thisDoesNotExist;

    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.thisDoesNotExistRemote({},
                                                       function(result, excp) {
                                                           theResult = result;
                                                           theExcp = excp;
                                                           Mainloop.quit('testDbus');
                                                       });
                      });

    Mainloop.run('testDbus');

    assertNotNull(theExcp);
    assertNull(theResult);
}

function testNonJsonFrobateStuff() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.nonJsonFrobateStuffRemote(42,
                                                          function(result, excp) {
                                                              theResult = result;
                                                              theExcp = excp;
                                                              Mainloop.quit('testDbus');
                                                          });
                      });

    Mainloop.run('testDbus');

    assertEquals("42 it is!", theResult);
    assertNull(theExcp);
}

function testNoInParameter() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.noInParameterRemote(function(result, excp) {
                                                        theResult = result;
                                                        theExcp = excp;
                                                        Mainloop.quit('testDbus');
                                                    });
                      });

    Mainloop.run('testDbus');

    assertEquals("Yes!", theResult);
    assertNull(theExcp);
}

function testMultipleInArgs() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.multipleInArgsRemote(1,2,3,4,5,
                                                     function(result, excp) {
                                                         theResult = result;
                                                         theExcp = excp;
                                                         Mainloop.quit('testDbus');
                                                     });
                      });

    Mainloop.run('testDbus');

    assertEquals("1 2 3 4 5", theResult);
    assertNull(theExcp);
}

function testNoReturnValue() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.noReturnValueRemote(function(result, excp) {
                                                        theResult = result;
                                                        theExcp = excp;
                                                        Mainloop.quit('testDbus');
                                                    });
                      });

    Mainloop.run('testDbus');

    assertEquals(undefined, theResult);
    assertNull(theExcp);
}

function testEmitSignal() {
    let theResult, theExcp;
    let signalReceived = 0;
    let signalArgument = null;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          let id = proxy.connect('signalFoo',
                            function(emitter, someString) {
                              signalReceived ++;
                              signalArgument = someString;

                              proxy.disconnect(id);
                          });
                          proxy.emitSignalRemote(function(result, excp) {
                                                     theResult = result;
                                                     theExcp = excp;
                                                     if (excp)
                                                         log("Signal emission exception: " + excp);
                                                     Mainloop.quit('testDbus');
                                                 });
                          });

    Mainloop.run('testDbus');

    assertUndefined('result should be undefined', theResult);
    assertNull('no exception set', theExcp);
    assertEquals('number of signals received', signalReceived, 1);
    assertEquals('signal argument', signalArgument, "foobar");

}

function testMultipleOutValues() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.multipleOutValuesRemote(function(result, excp) {
                                                            theResult = result;
                                                            theExcp = excp;
                                                            Mainloop.quit('testDbus');
                                                        });
                      });

    Mainloop.run('testDbus');

    assertEquals("Hello", theResult[0]);
    assertEquals("World", theResult[1]);
    assertEquals("!", theResult[2]);
    assertNull(theExcp);
}

function testOneArrayOut() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.oneArrayOutRemote(function(result, excp) {
                                                      theResult = result;
                                                      theExcp = excp;
                                                      Mainloop.quit('testDbus');
                                                  });
                      });

    Mainloop.run('testDbus');

    assertEquals("Hello", theResult[0]);
    assertEquals("World", theResult[1]);
    assertEquals("!", theResult[2]);
    assertNull(theExcp);
}

function testArrayOfArrayOut() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.arrayOfArrayOutRemote(function(result, excp) {
                                                          theResult = result;
                                                          theExcp = excp;
                                                          Mainloop.quit('testDbus');
                                                      });
                      });

    Mainloop.run('testDbus');

    let a1 = theResult[0];
    let a2 = theResult[1];

    assertEquals("Hello", a1[0]);
    assertEquals("World", a1[1]);

    assertEquals("World", a2[0]);
    assertEquals("Hello", a2[1]);;

    assertNull(theExcp);
}

function testMultipleArrayOut() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.multipleArrayOutRemote(function(result, excp) {
                                                           theResult = result;
                                                           theExcp = excp;
                                                           Mainloop.quit('testDbus');
                                                       });
                          });

    Mainloop.run('testDbus');

    let a1 = theResult[0];
    let a2 = theResult[1];

    assertEquals("Hello", a1[0]);
    assertEquals("World", a1[1]);

    assertEquals("World", a2[0]);
    assertEquals("Hello", a2[1]);;

    assertNull(theExcp);
}

/* We are returning an array but the signature says it's an integer,
 * so this should fail
 */
function testArrayOutBadSig() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.arrayOutBadSigRemote(function(result, excp) {
                                                         theResult = result;
                                                         theExcp = excp;
                                                         Mainloop.quit('testDbus');
                                                     });
                      });

    Mainloop.run('testDbus');
    assertNull(theResult);
    assertNotNull(theExcp);
}

function testSignatureLength() {
    const sigsAndLengths = [
        { sig: "", length: 0 },
        { sig: "i", length: 1 },
        { sig: "iii", length: 3 },
        { sig: "ai", length: 1 },
        { sig: "a{sv}", length: 1 },
        { sig: "aiaia{sv}", length: 3 },
        { sig: "(ai)as((ai)(a{sv}))", length: 3 },
        { sig: "iiisssiiiaiaiai(aiai)svvviavi", length: 20 }
    ];

    for (let i = 0; i < sigsAndLengths.length; i++) {
        let o = sigsAndLengths[i];
        let sigLen = DBus.signatureLength(o.sig);
        assertEquals(o.sig + ': ' + o.length + ' == ' + sigLen,
                     o.length, sigLen);
    }

    const invalidSig = "a";
    assertRaises('invalid signature', function () { DBus.signatureLength(invalidSig); });

    assertRaises('trying to measure length of an int', function () { DBus.signatureLength(5); });
    assertRaises('trying to measure length of undefined', function () { DBus.signatureLength(); });
}

function testChangingSig() {
    let theResultA, theExcpA;
    let theResultB, theExcpB;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.changingSigRemote(true,
                                                  function(result, excp) {
                                                      theResultA = result;
                                                      theExcpA = excp;
                                                  });
                      });

    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.changingSigRemote(false,
                                                  function(result, excp) {
                                                      theResultB = result;
                                                      theExcpB = excp;
                                                      Mainloop.quit('testDbus');
                                                  });
                      });

    Mainloop.run('testDbus');
    assertEquals("CHANGE", theResultA);
    assertNull(theExcpA);
    assertEquals(42, theResultB);
    assertNull(theExcpB);
}

function testAsyncImplementation() {
    let someString = "Hello world!";
    let someInt = 42;
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.echoRemote(someString, someInt,
                                           function(result, excp) {
                                               theResult = result;
                                               theExcp = excp;
                                               Mainloop.quit('testDbus');
                                           });
                      });

    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertNotNull(theResult);
    assertEquals(theResult[0], someString);
    assertEquals(theResult[1], someInt);
}

function testLocalMachineID() {
    let machineID = DBus.localMachineID;

    assertNotUndefined(machineID);
    assertNotNull(machineID);
}

function testGetReadOnlyProperty() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.GetRemote("PropReadOnly",
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertNotNull(theResult);
    assertEquals(true, theResult);
}

function testSetReadOnlyPropertyFails() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.SetRemote("PropReadOnly", "foo bar",
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');
    assertNotNull(theExcp);
    assertTrue(theExcp.message.indexOf('not writable') >= 0);
    assertNull(theResult);
}

function testReadWriteProperty() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.GetRemote("PropReadWrite",
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');

    assertEquals(PROP_READ_WRITE_INITIAL_VALUE,
                 DBus.session.exports.com.litl.Real['-impl-']._propReadWrite);

    assertNull(theExcp);
    assertNotNull(theResult);
    assertEquals(PROP_READ_WRITE_INITIAL_VALUE, theResult);

    theResult = null;
    theExcp = null;

    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.SetRemote("PropReadWrite", 371,
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertUndefined(theResult);

    theResult = null;
    theExcp = null;

    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.GetRemote("PropReadWrite",
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');

    assertEquals(371, DBus.session.exports.com.litl.Real['-impl-']._propReadWrite);

    assertNull(theExcp);
    assertNotNull(theResult);
    assertEquals(371, theResult);
}

function testWriteOnlyProperty() {
    let theResult, theExcp;

    assertEquals(PROP_WRITE_ONLY_INITIAL_VALUE,
                 DBus.session.exports.com.litl.Real['-impl-']._propWriteOnly);


    theResult = null;
    theExcp = null;

    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.SetRemote("PropWriteOnly", "a changed value",
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');

    assertNull(theExcp);
    assertUndefined(theResult);

    assertEquals("a changed value",
                 DBus.session.exports.com.litl.Real['-impl-']._propWriteOnly);

    // Test that it's not writable

    theResult = null;
    theExcp = null;

    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.GetRemote("PropWriteOnly",
                                          function(result, excp) {
                                              theResult = result;
                                              theExcp = excp;
                                              Mainloop.quit('testDbus');
                                          });
                      });

    Mainloop.run('testDbus');
    assertNotNull(theExcp);
    assertNull(theResult);

    assertTrue(theExcp.message.indexOf('not readable') >= 0);
}

// at the moment, the test asserts that GetAll is not implemented,
// of course we'd make this test that it works once it is implemented
function testGetAllProperties() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.GetAllRemote(function(result, excp) {
                                                 theResult = result;
                                                 theExcp = excp;
                                                 Mainloop.quit('testDbus');
                                             });
                      });

    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertNotNull(theResult);
    assertTrue('PropReadOnly' in theResult);
    assertTrue('PropReadWrite' in theResult);
    assertFalse('PropWriteOnly' in theResult);

    assertEquals(true,
                 theResult.PropReadOnly);
    assertEquals(DBus.session.exports.com.litl.Real['-impl-']._propReadWrite,
                 theResult.PropReadWrite);

    let count = 0;
    for (let p in theResult) {
        count += 1;
    }
    assertEquals(2, count);
}

function testGetMessageContextAsync() {
    let sender, serial;
    Mainloop.idle_add(function () {
                        let proxy = new Malarky();
                        proxy.noInParameterRemote(function (result, excp) {
                                                    let context = DBus.getCurrentMessageContext();
                                                    sender = context.sender;
                                                    serial = context.serial;
                                                    Mainloop.quit('testDbus');
                                                  });
                      });
    Mainloop.run('testDbus');
    assertTrue(typeof(sender) == 'string');
    assertTrue(typeof(serial) == 'number');
}

function testByteArrays() {
    let someString = "Hello\x00world!\x00\x00\x01\x02\x03";
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.byteArrayEchoRemote(someString,
                                                    function(result, excp) {
                                                        theResult = result;
                                                        theExcp = excp;
                                                        Mainloop.quit('testDbus');
                                                    });
                      });

    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertNotNull(theResult);
    assertEquals(someString, theResult);
}

function testBytes() {
    let someBytes = [ 0, 63, 234 ];
    let theResult, theExcp;
    for (let i = 0; i < someBytes.length; ++i) {
        theResult = null;
        theExcp = null;
        Mainloop.idle_add(function() {
                              let proxy = new Malarky();
                              proxy.byteEchoRemote(someBytes[i],
                                                   function(result, excp) {
                                                       theResult = result;
                                                       theExcp = excp;
                                                       Mainloop.quit('testDbus');
                                                   });
                          });

        Mainloop.run('testDbus');
        assertNull(theExcp);
        assertNotNull(theResult);
        assertEquals(someBytes[i], theResult);
    }
}

function testStructArray() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                      let proxy = new Malarky();
                      proxy.structArrayRemote(function(result, excp) {
                                                  theResult = result;
                                                  theExcp = excp;
                                                  Mainloop.quit('testDbus');
                                              });
                      });
    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertNotNull(theResult);
    assertEquals(theResult[0][0], 128);
    assertEquals(theResult[0][1], 123456);
    assertEquals(theResult[1][0], 42);
    assertEquals(theResult[1][1], 654321);
}

function testDictSignatures() {
    let someDict = {
        // should be a double after round trip except
        // JS_NewNumberValue() will convert it back to an int anyway
        // if the fractional part is 0
        aDouble: 10,
        // should be an integer after round trip
        anInteger: 10.5,
        // should remain a double
        aDoubleBeforeAndAfter: 10.5,
        // _dbus_signatures forces the wire type
        _dbus_signatures: {
            aDouble: 'd',
            anInteger: 'i',
            aDoubleBeforeAndAfter: 'd'
        }
    };
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new Malarky();
                          proxy.dictEchoRemote(someDict,
                                               function(result, excp) {
                                                   theResult = result;
                                                   theExcp = excp;
                                                   Mainloop.quit('testDbus');
                                               });
                      });

    Mainloop.run('testDbus');
    assertNull(theExcp);
    assertNotNull(theResult);

    // assert we did not send _dbus_signatures over the wire
    assertFalse('_dbus_signatures' in theResult);

    // verify the fractional part was dropped off int
    assertEquals(10, theResult['anInteger']);

    // and not dropped off a double
    assertEquals(10.5, theResult['aDoubleBeforeAndAfter']);

    // this assertion is useless, it will work
    // anyway if the result is really an int,
    // but it at least checks we didn't lose data
    assertEquals(10.0, theResult['aDouble']);
}

function testGetPropertySignatures() {
    let real = DBus.session.exports.com.litl.Real['-impl-'];
    let signatures = real.getDBusPropertySignatures(realIface.name);
    assertTrue('PropReadOnly' in signatures);
    assertTrue('PropWriteOnly' in signatures);
    assertTrue('PropReadWrite' in signatures);
    assertEquals('b', signatures.PropReadOnly);
    assertEquals('s', signatures.PropWriteOnly);
    assertEquals('v', signatures.PropReadWrite);

    // run again because getDBusPropertySignatures caches
    // the signatures, so be sure the caching doesn't
    // break things somehow
    signatures = real.getDBusPropertySignatures(realIface.name);
    assertTrue('PropReadOnly' in signatures);
    assertTrue('PropWriteOnly' in signatures);
    assertTrue('PropReadWrite' in signatures);
    assertEquals('b', signatures.PropReadOnly);
    assertEquals('s', signatures.PropWriteOnly);
    assertEquals('v', signatures.PropReadWrite);
}

function testIntrospectRoot() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new DBus.IntrospectableProxy(DBus.session,
                                                                   "com.litl.Real",
                                                                   "/");
                          proxy.IntrospectRemote(function(result, excp) {
                                                     theResult = result;
                                                     theExcp = excp;
                                                     Mainloop.quit('testDbus');
                                                 });
                      });

    Mainloop.run('testDbus');

    // JavaScript XML object apparently can't handle a DOCTYPE
    // declaration because it wants an element not a document,
    // so strip the doctype decl off
    let xml = new XML(theResult.replace(/<!DOCTYPE[^>]+>/, ''));
    assertEquals(1, xml.node.length());
    assertEquals("com", xml.node[0].@name.toString());
    assertEquals(0, xml.interface.length());
}

function testIntrospectCom() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new DBus.IntrospectableProxy(DBus.session,
                                                                   "com.litl.Real",
                                                                   "/com");
                          proxy.IntrospectRemote(function(result, excp) {
                                                     theResult = result;
                                                     theExcp = excp;
                                                     Mainloop.quit('testDbus');
                                                 });
                      });

    Mainloop.run('testDbus');

    // JavaScript XML object apparently can't handle a DOCTYPE
    // declaration because it wants an element not a document,
    // so strip the doctype decl off
    let xml = new XML(theResult.replace(/<!DOCTYPE[^>]+>/, ''));
    assertEquals(1, xml.node.length());
    assertEquals("litl", xml.node[0].@name.toString());
    assertEquals(0, xml.interface.length());
}

function testIntrospectLitl() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new DBus.IntrospectableProxy(DBus.session,
                                                                   "com.litl.Real",
                                                                   "/com/litl");
                          proxy.IntrospectRemote(function(result, excp) {
                                                     theResult = result;
                                                     theExcp = excp;
                                                     Mainloop.quit('testDbus');
                                                 });
                      });

    Mainloop.run('testDbus');

    // JavaScript XML object apparently can't handle a DOCTYPE
    // declaration because it wants an element not a document,
    // so strip the doctype decl off
    let xml = new XML(theResult.replace(/<!DOCTYPE[^>]+>/, ''));
    assertEquals(1, xml.node.length());
    assertEquals("Real", xml.node[0].@name.toString());
    assertEquals(0, xml.interface.length());
}

function testIntrospectReal() {
    let theResult, theExcp;
    Mainloop.idle_add(function() {
                          let proxy = new DBus.IntrospectableProxy(DBus.session,
                                                                   "com.litl.Real",
                                                                   "/com/litl/Real");
                          proxy.IntrospectRemote(function(result, excp) {
                                                     theResult = result;
                                                     theExcp = excp;
                                                     Mainloop.quit('testDbus');
                                                 });
                      });

    Mainloop.run('testDbus');

    // JavaScript XML object apparently can't handle a DOCTYPE
    // declaration because it wants an element not a document,
    // so strip the doctype decl off
    let xml = new XML(theResult.replace(/<!DOCTYPE[^>]+>/, ''));
    assertEquals(0, xml.node.length());
    // we get the 'realIface', plus 'Introspectable' and 'Properties'
    assertEquals(3, xml.interface.length());
    assertEquals('com.litl.Real', xml.interface[0].@name.toString());
    assertEquals(19, xml.interface[0].method.length());
    assertEquals(3, xml.interface[0].property.length());
    assertEquals(1, xml.interface[0].signal.length());
    assertEquals('org.freedesktop.DBus.Introspectable', xml.interface[1].@name.toString());
    assertEquals(1, xml.interface[1].method.length());
    assertEquals(0, xml.interface[1].property.length());
    assertEquals(0, xml.interface[1].signal.length());
    assertEquals('org.freedesktop.DBus.Properties', xml.interface[2].@name.toString());
    assertEquals(3, xml.interface[2].method.length());
    assertEquals(0, xml.interface[2].property.length());
    assertEquals(0, xml.interface[2].signal.length());
}

gjstestRun();
