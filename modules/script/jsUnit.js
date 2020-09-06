// SPDX-License-Identifier: MPL-1.1 OR GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2003 Edward Hieatt, edward@jsunit.net
// SPDX-FileCopyrightText: 2001-4 Edward Hieatt, edward@jsunit.net
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileContributor: @author Edward Hieatt, edward@jsunit.net

/*
 - JsUnit
*/

var JSUNIT_UNDEFINED_VALUE;
var JSUNIT_VERSION="2.1";
var isTestPageLoaded = false;

// GJS: introduce implicit variable to avoid exceptions
var top = null;

//hack for NS62 bug
function jsUnitFixTop() {
  var tempTop = top;
  if (!tempTop) {
    tempTop = window;
    while (typeof tempTop.parent !== 'undefined') {
      tempTop = tempTop.parent;
      if (tempTop.top && tempTop.top.jsUnitTestSuite) {
        tempTop = tempTop.top;
        break;
      }
    }
  }
  top = tempTop;
}

jsUnitFixTop();

function _displayStringForValue(aVar) {
  if (aVar === null)
    return 'null';

  if (aVar === top.JSUNIT_UNDEFINED_VALUE)
    return 'undefined';

  return aVar;
}

function fail(failureMessage) {
    throw new JsUnitException(null, failureMessage);
}

function error(errorMessage) {
    throw new Error(errorMessage);
}

function argumentsIncludeComments(expectedNumberOfNonCommentArgs, args) {
  return args.length == expectedNumberOfNonCommentArgs + 1;
}

function commentArg(expectedNumberOfNonCommentArgs, args) {
  if (argumentsIncludeComments(expectedNumberOfNonCommentArgs, args))
    return args[0];

  return null;
}

function nonCommentArg(desiredNonCommentArgIndex, expectedNumberOfNonCommentArgs, args) {
  return argumentsIncludeComments(expectedNumberOfNonCommentArgs, args) ?
    args[desiredNonCommentArgIndex] :
    args[desiredNonCommentArgIndex - 1];
}

function _validateArguments(expectedNumberOfNonCommentArgs, args) {
  if (!( args.length == expectedNumberOfNonCommentArgs ||
        (args.length == expectedNumberOfNonCommentArgs + 1 && typeof(args[0]) == 'string') ))
    error('Incorrect arguments passed to assert function');
}

function _assert(comment, booleanValue, failureMessage) {
  if (!booleanValue)
    throw new JsUnitException(comment, failureMessage);
}

function assert() {
  _validateArguments(1, arguments);
  var booleanValue=nonCommentArg(1, 1, arguments);

  if (typeof(booleanValue) != 'boolean')
    error('Bad argument to assert(boolean)');

  _assert(commentArg(1, arguments), booleanValue === true, 'Call to assert(boolean) with false');
}

function assertTrue() {
  _validateArguments(1, arguments);
  var booleanValue=nonCommentArg(1, 1, arguments);

  if (typeof(booleanValue) != 'boolean')
    error('Bad argument to assertTrue(boolean)');

  _assert(commentArg(1, arguments), booleanValue === true, 'Call to assertTrue(boolean) with false');
}

function assertFalse() {
  _validateArguments(1, arguments);
  var booleanValue=nonCommentArg(1, 1, arguments);

  if (typeof(booleanValue) != 'boolean')
    error('Bad argument to assertFalse(boolean)');

  _assert(commentArg(1, arguments), booleanValue === false, 'Call to assertFalse(boolean) with true');
}

function assertEquals() {
  _validateArguments(2, arguments);
  var var1=nonCommentArg(1, 2, arguments);
  var var2=nonCommentArg(2, 2, arguments);
  _assert(commentArg(2, arguments), var1 === var2, 'Expected ' + var1 + ' (' + typeof(var1) + ') but was ' + _displayStringForValue(var2) + ' (' + typeof(var2) + ')');
}

function assertNotEquals() {
  _validateArguments(2, arguments);
  var var1=nonCommentArg(1, 2, arguments);
  var var2=nonCommentArg(2, 2, arguments);
  _assert(commentArg(2, arguments), var1 !== var2, 'Expected not to be ' + _displayStringForValue(var2));
}

function assertNull() {
  _validateArguments(1, arguments);
  var aVar=nonCommentArg(1, 1, arguments);
  _assert(commentArg(1, arguments), aVar === null, 'Expected null but was ' + _displayStringForValue(aVar));
}

function assertNotNull() {
  _validateArguments(1, arguments);
  var aVar=nonCommentArg(1, 1, arguments);
  _assert(commentArg(1, arguments), aVar !== null, 'Expected not to be null');
}

function assertUndefined() {
  _validateArguments(1, arguments);
  var aVar=nonCommentArg(1, 1, arguments);
  _assert(commentArg(1, arguments), aVar === top.JSUNIT_UNDEFINED_VALUE, 'Expected undefined but was ' + _displayStringForValue(aVar));
}

function assertNotUndefined() {
  _validateArguments(1, arguments);
  var aVar=nonCommentArg(1, 1, arguments);
  _assert(commentArg(1, arguments), aVar !== top.JSUNIT_UNDEFINED_VALUE, 'Expected not to be undefined');
}

function assertNaN() {
  _validateArguments(1, arguments);
  var aVar=nonCommentArg(1, 1, arguments);
  _assert(commentArg(1, arguments), isNaN(aVar), 'Expected NaN');
}

function assertNotNaN() {
  _validateArguments(1, arguments);
  var aVar=nonCommentArg(1, 1, arguments);
  _assert(commentArg(1, arguments), !isNaN(aVar), 'Expected not NaN');
}

// GJS: assertRaises(function)
function assertRaises() {
    _validateArguments(1, arguments);
    var fun=nonCommentArg(1, 1, arguments);
    var exception;

    if (typeof(fun) != 'function')
        error("Bad argument to assertRaises(function)");

    var retval;
    try {
        retval = fun();
    } catch (e) {
        exception = e;
    }

    _assert(commentArg(1, arguments), exception !== top.JSUNIT_UNDEFINED_VALUE, "Call to assertRaises(function) did not raise an exception. Return value was " + _displayStringForValue(retval) + ' (' + typeof(retval) + ')');
}

function isLoaded() {
  return isTestPageLoaded;
}

function setUp() {
}

function tearDown() {
}

function getFunctionName(aFunction) {
  var name = aFunction.toString().match(/function (\w*)/)[1];

  if ((name == null) || (name.length == 0))
    name = 'anonymous';

  return name;
}

function parseErrorStack(excp)
{
  var stack = [];
  var name;

  if (!excp || !excp.stack)
  {
    return stack;
  }

  var stacklist = excp.stack.split('\n');

  for (var i = 0; i < stacklist.length - 1; i++)
  {
    var framedata = stacklist[i];

    name = framedata.match(/^(\w*)/)[1];
    if (!name) {
      name = 'anonymous';
    }

    var line = framedata.match(/(:\d+)$/)[1];
    if (line) {
        name += line;
    }

    stack[stack.length] = name;
  }
  // remove top level anonymous functions to match IE

  while (stack.length && stack[stack.length - 1] == 'anonymous')
  {
    stack.length = stack.length - 1;
  }
  return stack;
}

function JsUnitException(comment, message) {
  this.isJsUnitException = true;
  this.comment           = comment;
  this.message           = message;
  this.stack             = (new Error()).stack;
}

JsUnitException.prototype = Object.create(Error.prototype, {});

function warn() {
  if (top.tracer != null)
    top.tracer.warn(arguments[0], arguments[1]);
}

function inform() {
  if (top.tracer != null)
    top.tracer.inform(arguments[0], arguments[1]);
}

function info() {
  inform(arguments[0], arguments[1]);
}

function debug() {
  if (top.tracer != null)
    top.tracer.debug(arguments[0], arguments[1]);
}

function setjsUnitTracer(ajsUnitTracer) {
  top.tracer=ajsUnitTracer;
}

function trim(str) {
  if (str == null)
    return null;

  var startingIndex = 0;
  var endingIndex   = str.length-1;

  while (str.substring(startingIndex, startingIndex+1) == ' ')
    startingIndex++;

  while (str.substring(endingIndex, endingIndex+1) == ' ')
    endingIndex--;

  if (endingIndex < startingIndex)
    return '';

  return str.substring(startingIndex, endingIndex+1);
}

function isBlank(str) {
  return trim(str) == '';
}

// the functions push(anArray, anObject) and pop(anArray)
// exist because the JavaScript Array.push(anObject) and Array.pop()
// functions are not available in IE 5.0

function push(anArray, anObject) {
  anArray[anArray.length]=anObject;
}
function pop(anArray) {
  if (anArray.length>=1) {
    delete anArray[anArray.length - 1];
    anArray.length--;
  }
}

// safe, strict access to jsUnitParmHash
function jsUnitGetParm(name)
{
  if (typeof(top.jsUnitParmHash[name]) != 'undefined')
  {
    return top.jsUnitParmHash[name];
  }
  return null;
}

if (top && typeof(top.xbDEBUG) != 'undefined' && top.xbDEBUG.on && top.testManager)
{
  top.xbDebugTraceObject('top.testManager.containerTestFrame', 'JSUnitException');
  // asserts
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', '_displayStringForValue');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'error');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'argumentsIncludeComments');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'commentArg');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'nonCommentArg');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', '_validateArguments');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', '_assert');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assert');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertTrue');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertEquals');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertNotEquals');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertNull');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertNotNull');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertUndefined');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertNotUndefined');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertNaN');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'assertNotNaN');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'isLoaded');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'setUp');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'tearDown');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'getFunctionName');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'warn');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'inform');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'debug');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'setjsUnitTracer');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'trim');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'isBlank');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'newOnLoadEvent');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'push');
  top.xbDebugTraceFunction('top.testManager.containerTestFrame', 'pop');
}

function newOnLoadEvent() {
  isTestPageLoaded = true;
}

function jsUnitSetOnLoad(windowRef, onloadHandler)
{
  var isKonqueror = navigator.userAgent.indexOf('Konqueror/') != -1 ||
                    navigator.userAgent.indexOf('Safari/')    != -1;

  if (typeof(windowRef.attachEvent) != 'undefined') {
    // Internet Explorer, Opera
    windowRef.attachEvent("onload", onloadHandler);
  } else if (typeof(windowRef.addEventListener) != 'undefined' && !isKonqueror){
    // Mozilla, Konqueror
    // exclude Konqueror due to load issues
    windowRef.addEventListener("load", onloadHandler, false);
  } else if (typeof(windowRef.document.addEventListener) != 'undefined' && !isKonqueror) {
    // DOM 2 Events
    // exclude Mozilla, Konqueror due to load issues
    windowRef.document.addEventListener("load", onloadHandler, false);
  } else if (typeof(windowRef.onload) != 'undefined' && windowRef.onload) {
    windowRef.jsunit_original_onload = windowRef.onload;
    windowRef.onload = function() { windowRef.jsunit_original_onload(); onloadHandler(); };
  } else {
    // browsers that do not support windowRef.attachEvent or
    // windowRef.addEventListener will override a page's own onload event
    windowRef.onload=onloadHandler;
  }
}

// GJS: comment out as isLoaded() isn't terribly useful for us
//jsUnitSetOnLoad(window, newOnLoadEvent);

// GJS: entry point to run all functions named as test*, surrounded by
// calls to setUp() and tearDown()
function gjstestRun(window_, setUp, tearDown) {
  var propName;
  var rv = 0;
  var failures = [];
  if (!window_) window_ = window;
  if (!setUp) setUp = window_.setUp;
  if (!tearDown) tearDown = window_.tearDown;

  for (propName in window_) {
    if (!propName.match(/^test\w+/))
      continue;

    var testFunction = window_[propName];
    if (typeof(testFunction) != 'function')
      continue;

    log("running test " + propName);

    setUp();
    try {
      testFunction();
    } catch (e) {
      var result = null;
      if (typeof(e.isJsUnitException) != 'undefined' && e.isJsUnitException) {
        result = '';
        if (e.comment != null)
          result += ('"' + e.comment + '"\n');

        result += e.message;

        if (e.stack)
          result += '\n\nStack trace follows:\n' + e.stack;

        // assertion failure, kind of expected so just log it and flag the
        // whole test as failed
        log(result);
        rv = 1;
        failures.push(propName);
      }
      else {
        // unexpected error, let the shell handle it
        throw e;
      }
    }
    tearDown();
  }

  if (failures.length > 0) {
    log(failures.length + " tests failed in this file");
    log("Failures were: " + failures.join(", "));
  }

  // if gjstestRun() is the last call in a file, this becomes the
  // exit code of the test program, so 0 = success, 1 = failed
  return rv;
}
