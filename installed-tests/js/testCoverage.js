const JSUnit = imports.jsUnit;
const Coverage = imports.coverage;

function parseScriptForExpressionLines(script) {
    const ast = Reflect.parse(script);
    return Coverage.expressionLinesForAST(ast);
}

function assertArrayEquals(actual, expected, assertion) {
    if (actual.length != expected.length)
        throw "Arrays not equal length. Actual array was " +
                actual.length + " and Expected array was " +
                expected.length;

    for (let i = 0; i < actual.length; i++) {
        assertion(expected[i], actual[i]);
    }
}

function testExpressionLinesFoundForAssignmentExpressionSides() {
    let foundLinesOnBothExpressionSides =
        parseScriptForExpressionLines("var x;\n" +
                                      "x = (function() {\n" +
                                      "    return 10;\n" +
                                      "})();\n");
    assertArrayEquals(foundLinesOnBothExpressionSides,
                      [1, 2, 3],
                      JSUnit.assertEquals);
}


function testExpressionLinesFoundForLinesInsideFunctions() {
    let foundLinesInsideNamedFunction =
        parseScriptForExpressionLines("function f(a, b) {\n" +
                                      "    let x = a;\n" +
                                      "    let y = b;\n" +
                                      "    return x + y;\n" +
                                      "}\n" +
                                      "\n" +
                                      "var z = f(1, 2);\n");
    assertArrayEquals(foundLinesInsideNamedFunction,
                      [2, 3, 4, 7],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForLinesInsideAnonymousFunctions() {
    let foundLinesInsideAnonymousFunction =
        parseScriptForExpressionLines("var z = (function f(a, b) {\n" +
                                      "     let x = a;\n" +
                                      "     let y = b;\n" +
                                      "     return x + y;\n" +
                                      " })();\n");
    assertArrayEquals(foundLinesInsideAnonymousFunction,
                      [1, 2, 3, 4],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForBodyOfFunctionProperty() {
    let foundLinesInsideFunctionProperty =
        parseScriptForExpressionLines("var o = {\n" +
                                      "    foo: function () {\n" +
                                      "        let x = a;\n" +
                                      "    }\n" +
                                      "};\n");
    assertArrayEquals(foundLinesInsideFunctionProperty,
                      [1, 2, 3],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForCallArgsOfFunctionProperty() {
    let foundLinesInsideCallArgs =
        parseScriptForExpressionLines("function f(a) {\n" +
                                      "}\n" +
                                      "f({\n" +
                                      "    foo: function() {\n" +
                                      "        let x = a;\n" +
                                      "    }\n" +
                                      "});\n");
    assertArrayEquals(foundLinesInsideCallArgs,
                      [1, 3, 4, 5],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForMultilineCallArgs() {
    let foundLinesInsideMultilineCallArgs =
        parseScriptForExpressionLines("function f(a, b, c) {\n" +
                                      "}\n" +
                                      "f(1,\n" +
                                      "  2,\n" +
                                      "  3);\n");
    assertArrayEquals(foundLinesInsideMultilineCallArgs,
                      [1, 3, 4, 5],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForNewCallWithObject() {
    let foundLinesInsideObjectCallArg =
        parseScriptForExpressionLines("function f(o) {\n" +
                                      "}\n" +
                                      "let obj = {\n" +
                                      "    Name: new f({ a: 1,\n" +
                                      "                  b: 2,\n" +
                                      "                  c: 3\n" +
                                      "                })\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideObjectCallArg,
                      [1, 3, 4, 5, 6],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForWhileLoop() {
    let foundLinesInsideWhileLoop =
        parseScriptForExpressionLines("var a = 0;\n" +
                                      "while (a < 1) {\n" +
                                      "    let x = 0;\n" +
                                      "    let y = 1;\n" +
                                      "    a++;" +
                                      "\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideWhileLoop,
                      [1, 2, 3, 4, 5],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForTryCatchFinally() {
    let foundLinesInsideTryCatchFinally =
        parseScriptForExpressionLines("var a = 0;\n" +
                                      "try {\n" +
                                      "    a++;\n" +
                                      "} catch (e) {\n" +
                                      "    a++;\n" +
                                      "} finally {\n" +
                                      "    a++;\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideTryCatchFinally,
                      [1, 2, 3, 4, 5, 7],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForCaseStatements() {
    let foundLinesInsideCaseStatements =
        parseScriptForExpressionLines("var a = 0;\n" +
                                      "switch (a) {\n" +
                                      "case 1:\n" +
                                      "    a++;\n" +
                                      "    break;\n" +
                                      "case 2:\n" +
                                      "    a++;\n" +
                                      "    break;\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideCaseStatements,
                      [1, 2, 4, 5, 7, 8],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForLoop() {
    let foundLinesInsideLoop =
        parseScriptForExpressionLines("for (let i = 0; i < 1; i++) {\n" +
                                      "    let x = 0;\n" +
                                      "    let y = 1;\n" +
                                      "\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideLoop,
                      [1, 2, 3],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForIfExits() {
    let foundLinesInsideIfExits =
        parseScriptForExpressionLines("if (1 > 0) {\n" +
                                      "    let i = 0;\n" +
                                      "} else {\n" +
                                      "    let j = 1;\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideIfExits,
                      [1, 2, 4],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForFirstLineOfMultilineIfTests() {
    let foundLinesInsideMultilineIfTest =
        parseScriptForExpressionLines("if (1 > 0 &&\n" +
                                      "    2 > 0 &&\n" +
                                      "    3 > 0){\n" +
                                      "    let a = 3;\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideMultilineIfTest,
                      [1, 4],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForObjectPropertyLiterals() {
    let foundLinesInsideObjectPropertyLiterals =
        parseScriptForExpressionLines("var a = {\n" +
                                      "    Name: 'foo',\n" +
                                      "    Ex: 'bar'\n" +
                                      "};\n");
    assertArrayEquals(foundLinesInsideObjectPropertyLiterals,
                      [1, 2, 3],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForObjectPropertyFunction() {
    let foundLinesInsideObjectPropertyFunction =
        parseScriptForExpressionLines("var a = {\n" +
                                      "    Name: function() {},\n" +
                                      "};\n");
    assertArrayEquals(foundLinesInsideObjectPropertyFunction,
                      [1, 2],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForObjectPropertyObjectExpression() {
    let foundLinesInsideObjectPropertyObjectExpression =
        parseScriptForExpressionLines("var a = {\n" +
                                      "    Name: {},\n" +
                                      "};\n");
    assertArrayEquals(foundLinesInsideObjectPropertyObjectExpression,
                      [1, 2],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForObjectPropertyArrayExpression() {
    let foundLinesInsideObjectPropertyObjectExpression =
        parseScriptForExpressionLines("var a = {\n" +
                                      "    Name: [],\n" +
                                      "};\n");
    assertArrayEquals(foundLinesInsideObjectPropertyObjectExpression,
                      [1, 2],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForObjectArgsToReturn() {
    let foundLinesInsideObjectArgsToReturn =
        parseScriptForExpressionLines("var a = {\n" +
                                      "    Name: {},\n" +
                                      "};\n");
    assertArrayEquals(foundLinesInsideObjectArgsToReturn,
                      [1, 2],
                      JSUnit.assertEquals);
}

function testExpressionLinesFoundForObjectArgsToThrow() {
    let foundLinesInsideObjectArgsToThrow =
        parseScriptForExpressionLines("function f() {\n" +
                                      "    throw {\n" +
                                      "        a: 1,\n" +
                                      "        b: 2\n" +
                                      "    }\n" +
                                      "}\n");
    assertArrayEquals(foundLinesInsideObjectArgsToThrow,
                      [2, 3, 4],
                      JSUnit.assertEquals);
}


function parseScriptForFunctionNames(script) {
    const ast = Reflect.parse(script);
    return Coverage.functionsForAST(ast);
}

function functionDeclarationsEqual(actual, expected) {
    JSUnit.assertEquals(expected.name, actual.name);
    JSUnit.assertEquals(expected.line, actual.line);
    JSUnit.assertEquals(expected.n_params, actual.n_params);
}

function testFunctionsFoundForDeclarations() {
    let foundFunctionDeclarations =
        parseScriptForFunctionNames("function f1() {}\n" +
                                    "function f2() {}\n" +
                                    "function f3() {}\n");
    assertArrayEquals(foundFunctionDeclarations,
                      [
                          { name: "f1", line: 1, n_params: 0 },
                          { name: "f2", line: 2, n_params: 0 },
                          { name: "f3", line: 3, n_params: 0 }
                      ],
                      functionDeclarationsEqual);
}

function testFunctionsFoundForNestedFunctions() {
    let foundFunctions =
        parseScriptForFunctionNames("function f1() {\n" +
                                    "    let f2 = function() {\n" +
                                    "        let f3 = function() {\n" +
                                    "        }\n" +
                                    "    }\n" +
                                    "}\n");
    assertArrayEquals(foundFunctions,
                      [
                          { name: "f1", line: 1, n_params: 0 },
                          { name: null, line: 2, n_params: 0 },
                          { name: null, line: 3, n_params: 0 }
                      ],
                      functionDeclarationsEqual);
}

function testFunctionsFoundOnSameLineButDifferentiatedOnArgs() {
    /* Note the lack of newlines. This is all on
     * one line */
    let foundFunctionsOnSameLine =
        parseScriptForFunctionNames("function f1() {" +
                                    "    return (function (a) {" +
                                    "        return function (a, b) {}" +
                                    "    });" +
                                    "}");
    assertArrayEquals(foundFunctionsOnSameLine,
                      [
                          { name: "f1", line: 1, n_params: 0 },
                          { name: null, line: 1, n_params: 1 },
                          { name: null, line: 1, n_params: 2 }
                      ],
                      functionDeclarationsEqual);
}

function parseScriptForBranches(script) {
    const ast = Reflect.parse(script);
    return Coverage.branchesForAST(ast);
}

function branchInfoEqual(actual, expected) {
    JSUnit.assertEquals(expected.point, actual.point);
    assertArrayEquals(expected.exits, actual.exits, JSUnit.assertEquals);
}

function testBothBranchExitsFoundForSimpleBranch() {
    let foundBranchExitsForSimpleBranch =
        parseScriptForBranches("if (1) {\n" +
                               "    let a = 1;\n" +
                               "} else {\n" +
                               "    let b = 2;\n" +
                               "}\n");
    assertArrayEquals(foundBranchExitsForSimpleBranch,
                      [
                          { point: 1, exits: [2, 4] }
                      ],
                      branchInfoEqual);
}

function testSingleExitFoundForBranchWithOneConsequent() {
    let foundBranchExitsForSingleConsequentBranch =
        parseScriptForBranches("if (1) {\n" +
                               "    let a = 1.0;\n" +
                               "}\n");
    assertArrayEquals(foundBranchExitsForSingleConsequentBranch,
                      [
                          { point: 1, exits: [2] }
                      ],
                      branchInfoEqual);
}

function testMultipleBranchesFoundForNestedIfElseBranches() {
    let foundBranchesForNestedIfElseBranches =
        parseScriptForBranches("if (1) {\n" +
                               "    let a = 1.0;\n" +
                               "} else if (2) {\n" +
                               "    let b = 2.0;\n" +
                               "} else if (3) {\n" +
                               "    let c = 3.0;\n" +
                               "} else {\n" +
                               "    let d = 4.0;\n" +
                               "}\n");
    assertArrayEquals(foundBranchesForNestedIfElseBranches,
                      [
                          /* the 'else if' line is actually an
                           * exit for the first branch */
                          { point: 1, exits: [2, 3] },
                          { point: 3, exits: [4, 5] },
                          /* 'else' by itself is not executable,
                           * it is the block it contains whcih
                           * is */
                          { point: 5, exits: [6, 8] }
                      ],
                      branchInfoEqual);
}


function testSimpleTwoExitBranchWithoutBlocks() {
    let foundBranches =
        parseScriptForBranches("let a, b;\n" +
                               "if (1)\n" +
                               "    a = 1.0\n" +
                               "else\n" +
                               "    b = 2.0\n" +
                               "\n");
    assertArrayEquals(foundBranches,
                      [
                          { point: 2, exits: [3, 5] }
                      ],
                      branchInfoEqual);
}

function testNoBranchFoundIfConsequentWasEmpty() {
    let foundBranches =
        parseScriptForBranches("let a, b;\n" +
                               "if (1) {}\n");
    assertArrayEquals(foundBranches,
                      [],
                      branchInfoEqual);
}

function testSingleExitFoundIfOnlyAlternateExitDefined() {
    let foundBranchesForOnlyAlternateDefinition =
        parseScriptForBranches("let a, b;\n" +
                               "if (1) {}\n" +
                               "else\n" +
                               "    a++;\n");
    assertArrayEquals(foundBranchesForOnlyAlternateDefinition,
                      [
                          { point: 2, exits: [4] }
                      ],
                      branchInfoEqual);
}

function testImplicitBranchFoundForWhileStatement() {
    let foundBranchesForWhileStatement =
        parseScriptForBranches("while (1) {\n" +
                               "    let a = 1;\n" +
                               "}\n" +
                               "let b = 2;");
    assertArrayEquals(foundBranchesForWhileStatement,
                      [
                          { point: 1, exits: [2] }
                      ],
                      branchInfoEqual);
}

function testImplicitBranchFoundForDoWhileStatement() {
    let foundBranchesForDoWhileStatement =
        parseScriptForBranches("do {\n" +
                               "    let a = 1;\n" +
                               "} while (1)\n" +
                               "let b = 2;");
    assertArrayEquals(foundBranchesForDoWhileStatement,
                      [
                          /* For do-while loops the branch-point is
                           * at the 'do' condition and not the
                           * 'while' */
                          { point: 1, exits: [2] }
                      ],
                      branchInfoEqual);
}

function testAllExitsFoundForCaseStatements() {
    let foundExitsInCaseStatement =
        parseScriptForBranches("let a = 1;\n" +
                               "switch (1) {\n" +
                               "case '1':\n" +
                               "    a++;\n" +
                               "    break;\n" +
                               "case '2':\n" +
                               "    a++\n" +
                               "    break;\n" +
                               "default:\n" +
                               "    a++\n" +
                               "    break;\n" +
                               "}\n");
    assertArrayEquals(foundExitsInCaseStatement,
                      [
                          /* There are three potential exits here */
                          { point: 2, exits: [4, 7, 10] }
                      ],
                      branchInfoEqual);
}

function testAllExitsFoundForFallthroughCaseStatements() {
    let foundExitsInCaseStatement =
        parseScriptForBranches("let a = 1;\n" +
                               "switch (1) {\n" +
                               "case '1':\n" +
                               "case 'a':\n" +
                               "case 'b':\n" +
                               "    a++;\n" +
                               "    break;\n" +
                               "case '2':\n" +
                               "    a++\n" +
                               "    break;\n" +
                               "default:\n" +
                               "    a++\n" +
                               "    break;\n" +
                               "}\n");
    assertArrayEquals(foundExitsInCaseStatement,
                      [
                          /* There are three potential exits here */
                          { point: 2, exits: [6, 9, 12] }
                      ],
                      branchInfoEqual);
}

function testAllNoExitsFoundForCaseStatementsWithNoopLabels() {
    let foundExitsInCaseStatement =
        parseScriptForBranches("let a = 1;\n" +
                               "switch (1) {\n" +
                               "case '1':\n" +
                               "case '2':\n" +
                               "default:\n" +
                               "}\n");
    assertArrayEquals(foundExitsInCaseStatement,
                      [],
                      branchInfoEqual);
}


function testGetNumberOfLinesInScript() {
    let script = "\n\n";
    let number = Coverage._getNumberOfLinesForScript(script);
    JSUnit.assertEquals(3, number);
}

function testZeroExpressionLinesToCounters() {
    let expressionLines = [];
    let nLines = 1;
    let counters = Coverage._expressionLinesToCounters(expressionLines, nLines);

    assertArrayEquals([undefined], counters, JSUnit.assertEquals);
}

function testSingleExpressionLineToCounters() {
    let expressionLines = [1, 2];
    let nLines = 4;
    let counters = Coverage._expressionLinesToCounters(expressionLines, nLines);

    assertArrayEquals([undefined, 0, 0, undefined], counters, JSUnit.assertEquals);
}

const MockFoundBranches = [
    {
        point: 5,
        exits: [6, 8]
    },
    {
        point: 1,
        exits: [2, 4]
    }
];

const MockNLines = 9;

function testGetsSameNumberOfCountersAsNLines() {
    let counters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    JSUnit.assertEquals(MockNLines, counters.length);
}

function testEmptyArrayReturnedForNoBranches() {
    let counters = Coverage._branchesToBranchCounters([], 1);
    assertArrayEquals([undefined], counters, JSUnit.assertEquals);
}

function testBranchesOnLinesForArrayIndicies() {
    let counters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    JSUnit.assertNotEquals(undefined, counters[1]);
    JSUnit.assertNotEquals(undefined, counters[5]);
}

function testExitsSetForBranch() {
    let counters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    let countersForFirstBranch = counters[1];

    assertArrayEquals(countersForFirstBranch.exits,
                      [
                          { line: 2, hitCount: 0 },
                          { line: 4, hitCount: 0 }
                      ],
                      function(expectedExit, actualExit) {
                          JSUnit.assertEquals(expectedExit.line, actualExit.line);
                          JSUnit.assertEquals(expectedExit.hitCount, actualExit.hitCount);
                      });
}

function testLastExitIsSetToHighestExitStartLine() {
    let counters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    let countersForFirstBranch = counters[1];

    JSUnit.assertEquals(4, countersForFirstBranch.lastExit);
}

function testHitIsAlwaysInitiallyFalse() {
    let counters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    let countersForFirstBranch = counters[1];

    JSUnit.assertEquals(false, countersForFirstBranch.hit);
}

function testFunctionForKeyFromFunctionWithNameMatchesSchema() {
    let expectedFunctionKey = 'f:1:2';
    let functionKeyForFunctionName =
        Coverage._getFunctionKeyFromReflectedFunction({ name: 'f',
                                                        line: 1,
                                                        n_params: 2 });

    JSUnit.assertEquals(expectedFunctionKey, functionKeyForFunctionName);
}

function testFunctionKeyFromFunctionWithoutNameIsAnonymous() {
    let expectedFunctionKey = '(anonymous):2:3';
    let functionKeyForAnonymousFunction =
        Coverage._getFunctionKeyFromReflectedFunction({ name: null,
                                                        line: 2,
                                                        n_params: 3 });

    JSUnit.assertEquals(expectedFunctionKey, functionKeyForAnonymousFunction);
}



function testFunctionCounterMapReturnedForFunctionKeys() {
    let func = {
        name: 'name',
        line: 1,
        n_params: 0
    };

    let functionKey = Coverage._getFunctionKeyFromReflectedFunction(func);
    let functionCounters = Coverage._functionsToFunctionCounters([func]);

    JSUnit.assertEquals(0, functionCounters[functionKey].hitCount);
}

function testKnownFunctionsArrayPopulatedForFunctions() {
    let functions = [
        { line: 1 },
        { line: 2 }
    ];

    let knownFunctionsArray = Coverage._populateKnownFunctions(functions, 4);

    assertArrayEquals(knownFunctionsArray,
                      [undefined, true, true, undefined],
                      JSUnit.assertEquals);
}

function testIncrementFunctionCountersForFunctionOnSameExecutionStartLine() {
    let functionKey = 'f:1:0';
    let functionCounters = {};

    functionCounters[functionKey] = { hitCount: 0 };

    Coverage._incrementFunctionCounters(functionCounters, null, 'f', 1, 0);

    JSUnit.assertEquals(functionCounters[functionKey].hitCount, 1);
}

function testIncrementFunctionCountersForFunctionOnEarlierStartLine() {
    let functions = [
        {
            name: 'name',
            line: 1,
            n_params: 0
        }
    ];
    let functionKey = Coverage._getFunctionKeyFromReflectedFunction(functions[0]);
    let knownFunctionsArray = Coverage._populateKnownFunctions(functions, 3);
    let functionCounters = Coverage._functionsToFunctionCounters(functions);

    /* We're entering at line two, but the function definition was actually
     * at line one */
    Coverage._incrementFunctionCounters(functionCounters, knownFunctionsArray, 'name', 2, 0);

    JSUnit.assertEquals(functionCounters[functionKey].hitCount, 1);
}

function testIncrementFunctionCountersThrowsErrorOnUnexpectedFunction() {
    let functions = [
        {
            name: 'name',
            line: 1,
            n_params: 0
        }
    ];
    let functionKey = Coverage._getFunctionKeyFromReflectedFunction(functions[0]);
    let knownFunctionsArray = Coverage._populateKnownFunctions(functions, 3);
    let functionCounters = Coverage._functionsToFunctionCounters(functions);

    /* We're entering at line two, but the function definition was actually
     * at line one */
    JSUnit.assertRaises(function() {
        Coverage._incrementFunctionCounters(functionCounters,
                                            knownFunctionsArray,
                                            'doesnotexist',
                                            2,
                                            0);
    });
}

function testIncrementExpressionCountersThrowsIfLineOutOfRange() {
    let expressionCounters = [
        undefined,
        0
    ];

    JSUnit.assertRaises(function() {
        Coverage._incrementExpressionCounters(expressionCounters, 2);
    });
}

function testIncrementExpressionCountersIncrementsIfInRange() {
    let expressionCounters = [
        undefined,
        0
    ];

    Coverage._incrementExpressionCounters(expressionCounters, 1);
    JSUnit.assertEquals(1, expressionCounters[1]);
}

function testWarnsIfWeHitANonExecutableLine() {
    let expressionCounters = [
        undefined,
        0,
        undefined
    ];

    let wasCalledContainer = { calledWith: undefined };
    let reporter = function(cont) {
        let container = cont;
        return function(line) {
            container.calledWith = line;
        };
    } (wasCalledContainer);

    Coverage._incrementExpressionCounters(expressionCounters, 2, reporter);
    JSUnit.assertEquals(wasCalledContainer.calledWith, 2);
    JSUnit.assertEquals(expressionCounters[2], 1);
}

function testBranchTrackerSetsBranchToHitOnPointExecution() {
    let branchCounters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    let branchTracker = new Coverage._BranchTracker(branchCounters);

    branchTracker.incrementBranchCounters(1);

    JSUnit.assertEquals(true, branchCounters[1].hit);
}

function testBranchTrackerSetsExitToHitOnExecution() {
    let branchCounters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    let branchTracker = new Coverage._BranchTracker(branchCounters);

    branchTracker.incrementBranchCounters(1);
    branchTracker.incrementBranchCounters(2);

    JSUnit.assertEquals(1, branchCounters[1].exits[0].hitCount);
}

function testBranchTrackerFindsNextBranch() {
    let branchCounters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
    let branchTracker = new Coverage._BranchTracker(branchCounters);

    branchTracker.incrementBranchCounters(1);
    branchTracker.incrementBranchCounters(2);
    branchTracker.incrementBranchCounters(5);

    JSUnit.assertEquals(true, branchCounters[5].hit);
}

function testConvertFunctionCountersToArray() {
    let functionsMap = {};

    functionsMap['(anonymous):2:0'] = { hitCount: 1 };
    functionsMap['name:1:0'] = { hitCount: 0 };

    let expectedFunctionCountersArray = [
        { name: '(anonymous):2:0', hitCount: 1 },
        { name: 'name:1:0', hitCount: 0 }
    ];

    let convertedFunctionCounters = Coverage._convertFunctionCountersToArray(functionsMap);

    assertArrayEquals(expectedFunctionCountersArray,
                      convertedFunctionCounters,
                      function(expected, actual) {
                          JSUnit.assertEquals(expected.name, actual.name);
                          JSUnit.assertEquals(expected.hitCount, actual.hitCount);
                      });
}

function testConvertFunctionCountersToArrayIsSorted() {
    let functionsMap = {};

    functionsMap['name:1:0'] = { hitCount: 0 };
    functionsMap['(anonymous):2:0'] = { hitCount: 1 };

    let expectedFunctionCountersArray = [
        { name: '(anonymous):2:0', hitCount: 1 },
        { name: 'name:1:0', hitCount: 0 }
    ];

    let convertedFunctionCounters = Coverage._convertFunctionCountersToArray(functionsMap);

    assertArrayEquals(expectedFunctionCountersArray,
                      convertedFunctionCounters,
                      function(expected, actual) {
                          JSUnit.assertEquals(expected.name, actual.name);
                          JSUnit.assertEquals(expected.hitCount, actual.hitCount);
                      });
}

const MockFiles = {
    'filename': "let f = function() { return 1; };"
};

const MockFilenames = Object.keys(MockFiles);

Coverage.getFileContents = function(filename) {
    if (MockFiles[filename])
        return MockFiles[filename];
    throw new Error("Non existent");
};

function testCoverageStatisticsContainerFetchesValidStatisticsForFile() {
    let container = new Coverage.CoverageStatisticsContainer(MockFilenames);
    let statistics = container.fetchStatistics('filename');

    JSUnit.assertNotEquals(undefined, statistics);
}

function testCoverageStatisticsContainerThrowsForNonExistingFile() {
    let container = new Coverage.CoverageStatisticsContainer(MockFilenames);

    JSUnit.assertRaises(function() {
        container.fetchStatistics('nonexistent');
    });

}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
