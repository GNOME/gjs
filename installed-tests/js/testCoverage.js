const Coverage = imports.coverage;

describe('Coverage.expressionLinesForAST', function () {
    let testTable = {
        'works with no trailing newline': [
            "let x;\n" +
            "let y;",
            [1, 2],
        ],

        'finds lines on both sides of an assignment expression': [
            "var x;\n" +
            "x = (function() {\n" +
            "    return 10;\n" +
            "})();\n",
            [1, 2, 3],
        ],

        'finds lines inside functions': [
            "function f(a, b) {\n" +
            "    let x = a;\n" +
            "    let y = b;\n" +
            "    return x + y;\n" +
            "}\n" +
            "\n" +
            "var z = f(1, 2);\n",
            [2, 3, 4, 7],
        ],

        'finds lines inside anonymous functions': [
            "var z = (function f(a, b) {\n" +
            "     let x = a;\n" +
            "     let y = b;\n" +
            "     return x + y;\n" +
            " })();\n",
            [1, 2, 3, 4],
        ],

        'finds lines inside body of function property': [
            "var o = {\n" +
            "    foo: function() {\n" +
            "        let x = a;\n" +
            "    }\n" +
            "};\n",
            [1, 2, 3],
        ],

        'finds lines inside arguments of function property': [
            "function f(a) {\n" +
            "}\n" +
            "f({\n" +
            "    foo: function() {\n" +
            "        let x = a;\n" +
            "    }\n" +
            "});\n",
            [1, 3, 4, 5],
        ],

        'finds lines inside multiline function arguments': [
            "function f(a, b, c) {\n" +
            "}\n" +
            "f(1,\n" +
            "  2,\n" +
            "  3);\n",
            [1, 3, 4, 5],
        ],

        'finds lines inside function argument that is an object': [
            "function f(o) {\n" +
            "}\n" +
            "let obj = {\n" +
            "    Name: new f({ a: 1,\n" +
            "                  b: 2,\n" +
            "                  c: 3\n" +
            "                })\n" +
            "}\n",
            [1, 3, 4, 5, 6],
        ],

        'finds lines inside a while loop': [
            "var a = 0;\n" +
            "while (a < 1) {\n" +
            "    let x = 0;\n" +
            "    let y = 1;\n" +
            "    a++;" +
            "\n" +
            "}\n",
            [1, 2, 3, 4, 5],
        ],

        'finds lines inside try, catch, and finally': [
            "var a = 0;\n" +
            "try {\n" +
            "    a++;\n" +
            "} catch (e) {\n" +
            "    a++;\n" +
            "} finally {\n" +
            "    a++;\n" +
            "}\n",
            [1, 2, 3, 4, 5, 7],
        ],

        'finds lines inside case statements': [
            "var a = 0;\n" +
            "switch (a) {\n" +
            "case 1:\n" +
            "    a++;\n" +
            "    break;\n" +
            "case 2:\n" +
            "    a++;\n" +
            "    break;\n" +
            "}\n",
            [1, 2, 4, 5, 7, 8],
        ],

        'finds lines inside case statements with character cases': [
            "var a = 'a';\n" +
            "switch (a) {\n" +
            "case 'a':\n" +
            "    a++;\n" +
            "    break;\n" +
            "case 'b':\n" +
            "    a++;\n" +
            "    break;\n" +
            "}\n",
            [1, 2, 4, 5, 7, 8],
        ],

        'finds lines inside a for loop': [
            "for (let i = 0; i < 1; i++) {\n" +
            "    let x = 0;\n" +
            "    let y = 1;\n" +
            "\n" +
            "}\n",
            [1, 2, 3],
        ],

        'finds lines inside if-statement branches': [
            "if (1 > 0) {\n" +
            "    let i = 0;\n" +
            "} else {\n" +
            "    let j = 1;\n" +
            "}\n",
            [1, 2, 4],
        ],

        'finds all lines of multiline if-conditions': [
            "if (1 > 0 &&\n" +
            "    2 > 0 &&\n" +
            "    3 > 0) {\n" +
            "    let a = 3;\n" +
            "}\n",
            [1, 2, 3, 4],
        ],

        'finds lines for object property literals': [
            "var a = {\n" +
            "    Name: 'foo',\n" +
            "    Ex: 'bar'\n" +
            "};\n",
            [1, 2, 3],
        ],

        'finds lines for function-valued object properties': [
            "var a = {\n" +
            "    Name: function() {},\n" +
            "};\n",
            [1, 2],
        ],

        'finds lines inside object-valued object properties': [
            "var a = {\n" +
            "    Name: {},\n" +
            "};\n",
            [1, 2],
        ],

        'finds lines inside array-valued object properties': [
            "var a = {\n" +
            "    Name: [],\n" +
            "};\n",
            [1, 2],
        ],

        'finds lines inside object-valued argument to return statement': [
            "function f() {\n" +
            "    return {};\n" +
            "}\n",
            [2],
        ],

        'finds lines inside object-valued argument to throw statement': [
            "function f() {\n" +
            "    throw {\n" +
            "        a: 1,\n" +
            "        b: 2\n" +
            "    }\n" +
            "}\n",
            [2, 3, 4],
        ],
    };

    Object.keys(testTable).forEach(testcase => {
        it(testcase, function () {
            const ast = Reflect.parse(testTable[testcase][0]);
            let foundLines = Coverage.expressionLinesForAST(ast);
            expect(foundLines).toEqual(testTable[testcase][1]);
        });
    });
});

describe('Coverage.functionsForAST', function () {
    let testTable = {
        'works with no trailing newline': [
            "function f1() {}\n" +
            "function f2() {}",
            [
                { key: "f1:1:0", line: 1, n_params: 0 },
                { key: "f2:2:0", line: 2, n_params: 0 },
            ],
        ],

        'finds functions': [
            "function f1() {}\n" +
            "function f2() {}\n" +
            "function f3() {}\n",
            [
                { key: "f1:1:0", line: 1, n_params: 0 },
                { key: "f2:2:0", line: 2, n_params: 0 },
                { key: "f3:3:0", line: 3, n_params: 0 }
            ],
        ],

        'finds nested functions': [
            "function f1() {\n" +
            "    let f2 = function() {\n" +
            "        let f3 = function() {\n" +
            "        }\n" +
            "    }\n" +
            "}\n",
            [
                { key: "f1:1:0", line: 1, n_params: 0 },
                { key: "(anonymous):2:0", line: 2, n_params: 0 },
                { key: "(anonymous):3:0", line: 3, n_params: 0 }
            ],
        ],

        /* Note the lack of newlines. This is all on one line */
        'finds functions on the same line but with different arguments': [
            "function f1() {" +
            "    return (function(a) {" +
            "        return function(a, b) {}" +
            "    });" +
            "}",
            [
                { key: "f1:1:0", line: 1, n_params: 0 },
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:2", line: 1, n_params: 2 }
            ],
        ],

        'finds functions inside an array expression': [
            "let a = [function() {}];\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 },
            ],
        ],

        'finds functions inside an arrow expression': [
            "(a) => (function() {})();\n",
            [
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside a sequence': [
            "(function(a) {})()," +
            "(function(a, b) {})();\n",
            [
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:2", line: 1, n_params: 2 },
            ],
        ],

        'finds functions inside a unary expression': [
            "let a = (function() {}())++;\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 },
            ],
        ],

        'finds functions inside a binary expression': [
            "let a = function(a) {}() +" +
            " function(a, b) {}();\n",
            [
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:2", line: 1, n_params: 2 }
            ],
        ],

        'finds functions inside an assignment expression': [
            "let a = function() {}();\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside a reflexive assignment expression': [
            "let a;\n" +
            "a += function() {}();\n",
            [
                { key: "(anonymous):2:0", line: 2, n_params: 0 }
            ],
        ],

        'finds functions inside if-statement conditions': [
            "if (function(a) {}(a) >" +
            "    function(a, b) {}(a, b)) {}\n",
            [
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:2", line: 1, n_params: 2 }
            ],
        ],

        'finds functions inside while-statement conditions': [
            "while (function(a) {}(a) >" +
            "       function(a, b) {}(a, b)) {};\n",
            [
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:2", line: 1, n_params: 2 }
            ],
        ],

        'finds functions inside for-statement initializer': [
            "for (function() {}; ;) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        /* SpiderMonkey parses for (let i = <init>; <cond>; <update>) as though
         * they were let i = <init> { for (; <cond> <update>) } so test the
         * LetStatement initializer case too */
        'finds functions inside let-statement in for-statement initializer': [
            "for (let i = function() {}; ;) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside var-statement inside for-statement initializer': [
            "for (var i = function() {}; ;) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside for-statement condition': [
            "for (; function() {}();) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside for-statement increment': [
            "for (; ;function() {}()) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside for-in-statement': [
            "for (let x in function() {}()) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside for-each statement': [
            "for each (x in function() {}()) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions inside for-of statement': [
            "for (x of (function() {}())) {}\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds function literals used as an object': [
            "f = function() {}.bind();\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds function literals used as an object in a dynamic property expression': [
            "f = function() {}['bind']();\n",
            [
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions on either side of a logical expression': [
            "let f = function(a) {} ||" +
            " function(a, b) {};\n",
            [
                { key: "(anonymous):1:1", line: 1, n_params: 1 },
                { key: "(anonymous):1:2", line: 1, n_params: 2 }
            ],
        ],

        'finds functions on either side of a conditional expression': [
            "let a\n" +
            "let f = a ? function(a) {}() :" +
            " function(a, b) {}();\n",
            [
                { key: "(anonymous):2:1", line: 2, n_params: 1 },
                { key: "(anonymous):2:2", line: 2, n_params: 2 }
            ],
        ],

        'finds functions as the argument of a yield statement': [
            "function a() { yield function (){} };\n",
            [
                { key: "a:1:0", line: 1, n_params: 0 },
                { key: "(anonymous):1:0", line: 1, n_params: 0 }
            ],
        ],

        'finds functions in an array comprehension body': [
            "let a = new Array(1);\n" +
            "let b = [function() {} for (i of a)];\n",
            [
                { key: "(anonymous):2:0", line: 2, n_params: 0 }
            ],
        ],

        'finds functions in an array comprehension block': [
            "let a = new Array(1);\n" +
            "let b = [i for (i of function() {})];\n",
            [
                { key: "(anonymous):2:0", line: 2, n_params: 0 }
            ],
        ],

        'finds functions in an array comprehension filter': [
            "let a = new Array(1);\n" +
            "let b = [i for (i of a)" +
            "if (function() {}())];\n",
            [
                { key: "(anonymous):2:0", line: 2, n_params: 0 }
            ],
        ],
    };

    Object.keys(testTable).forEach(testcase => {
        it(testcase, function () {
            const ast = Reflect.parse(testTable[testcase][0]);
            let foundFuncs = Coverage.functionsForAST(ast);
            expect(foundFuncs).toEqual(testTable[testcase][1]);
        });
    });
});

describe('Coverage.branchesForAST', function () {
    let testTable = {
        'works with no trailing newline': [
            "if (1) { let a = 1; }",
            [
                { point: 1, exits: [1] },
            ],
        ],

        'finds both branch exits for a simple branch': [
            "if (1) {\n" +
            "    let a = 1;\n" +
            "} else {\n" +
            "    let b = 2;\n" +
            "}\n",
            [
                { point: 1, exits: [2, 4] }
            ],
        ],

        'finds a single exit for a branch with one consequent': [
            "if (1) {\n" +
            "    let a = 1.0;\n" +
            "}\n",
            [
                { point: 1, exits: [2] }
            ],
        ],

        'finds multiple exits for nested if-else branches': [
            "if (1) {\n" +
            "    let a = 1.0;\n" +
            "} else if (2) {\n" +
            "    let b = 2.0;\n" +
            "} else if (3) {\n" +
            "    let c = 3.0;\n" +
            "} else {\n" +
            "    let d = 4.0;\n" +
            "}\n",
            [
                // the 'else if' line is actually an exit for the first branch
                { point: 1, exits: [2, 3] },
                { point: 3, exits: [4, 5] },
                // 'else' by itself is not executable, it is the block it
                // contains which is
                { point: 5, exits: [6, 8] }
            ],
        ],

        'finds a simple two-exit branch without blocks': [
            "let a, b;\n" +
            "if (1)\n" +
            "    a = 1.0\n" +
            "else\n" +
            "    b = 2.0\n" +
            "\n",
            [
                { point: 2, exits: [3, 5] }
            ],
        ],

        'does not find a branch if the consequent was empty': [
            "let a, b;\n" +
            "if (1) {}\n",
            [],
        ],

        'finds a single exit if only the alternate exit was defined': [
            "let a, b;\n" +
            "if (1) {}\n" +
            "else\n" +
            "    a++;\n",
            [
                { point: 2, exits: [4] }
            ],
        ],

        'finds an implicit branch for while statement': [
            "while (1) {\n" +
            "    let a = 1;\n" +
            "}\n" +
            "let b = 2;",
            [
                { point: 1, exits: [2] }
            ],
        ],

        'finds an implicit branch for a do-while statement': [
            "do {\n" +
            "    let a = 1;\n" +
            "} while (1)\n" +
            "let b = 2;",
            [
                // For do-while loops the branch-point is at the 'do' condition
                // and not the 'while'
                { point: 1, exits: [2] }
            ],
        ],

        'finds all exits for case statements': [
            "let a = 1;\n" +
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
            "}\n",
            [
                /* There are three potential exits here */
                { point: 2, exits: [4, 7, 10] }
            ],
        ],

        'finds all exits for case statements with fallthrough': [
            "let a = 1;\n" +
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
            "}\n",
            [
                /* There are three potential exits here */
                { point: 2, exits: [6, 9, 12] }
            ],
        ],

        'finds no exits for case statements with only no-ops': [
            "let a = 1;\n" +
            "switch (1) {\n" +
            "case '1':\n" +
            "case '2':\n" +
            "default:\n" +
            "}\n",
            [],
        ],
    };

    Object.keys(testTable).forEach(testcase => {
        it(testcase, function () {
            const ast = Reflect.parse(testTable[testcase][0]);
            let foundBranchExits = Coverage.branchesForAST(ast);
            expect(foundBranchExits).toEqual(testTable[testcase][1]);
        });
    });
});

describe('Coverage', function () {
    it('gets the number of lines in the script', function () {
        let script = "\n\n";
        let number = Coverage._getNumberOfLinesForScript(script);
        expect(number).toEqual(3);
    });

    it('turns zero expression lines into counters', function () {
        let expressionLines = [];
        let nLines = 1;
        let counters = Coverage._expressionLinesToCounters(expressionLines, nLines);

        expect(counters).toEqual([undefined, undefined]);
    });

    it('turns a single expression line into counters', function () {
        let expressionLines = [1, 2];
        let nLines = 4;
        let counters = Coverage._expressionLinesToCounters(expressionLines, nLines);

        expect(counters).toEqual([undefined, 0, 0, undefined, undefined]);
    });

    it('returns empty array for no branches', function () {
        let counters = Coverage._branchesToBranchCounters([], 1);
        expect(counters).toEqual([undefined, undefined]);
    });

    describe('branch counters', function () {
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

        let counters;
        beforeEach(function () {
            counters = Coverage._branchesToBranchCounters(MockFoundBranches, MockNLines);
        });

        it('gets same number of counters as number of lines plus one', function () {
            expect(counters.length).toEqual(MockNLines + 1);
        });

        it('branches on lines for array indices', function () {
            expect(counters[1]).toBeDefined();
            expect(counters[5]).toBeDefined();
        });

        it('sets exits for branch', function () {
            expect(counters[1].exits).toEqual([
                { line: 2, hitCount: 0 },
                { line: 4, hitCount: 0 },
            ]);
        });

        it('sets last exit to highest exit start line', function () {
            expect(counters[1].lastExit).toEqual(4);
        });

        it('always has hit initially false', function () {
            expect(counters[1].hit).toBeFalsy();
        });

        describe('branch tracker', function () {
            let branchTracker;
            beforeEach(function () {
                branchTracker = new Coverage._BranchTracker(counters);
            });

            it('sets branch to hit on point execution', function () {
                branchTracker.incrementBranchCounters(1);
                expect(counters[1].hit).toBeTruthy();
            });

            it('sets exit to hit on execution', function () {
                branchTracker.incrementBranchCounters(1);
                branchTracker.incrementBranchCounters(2);
                expect(counters[1].exits[0].hitCount).toEqual(1);
            });

            it('finds next branch', function () {
                branchTracker.incrementBranchCounters(1);
                branchTracker.incrementBranchCounters(2);
                branchTracker.incrementBranchCounters(5);
                expect(counters[5].hit).toBeTruthy();
            });
        });
    });

    it('function key from function with name matches schema', function () {
        let functionKeyForFunctionName =
            Coverage._getFunctionKeyFromReflectedFunction({
                id: {
                    name: 'f'
                },
                loc: {
                  start: {
                      line: 1
                  }
                },
                params: ['a', 'b']
            });
        expect(functionKeyForFunctionName).toEqual('f:1:2');
    });

    it('function key from function without name is anonymous', function () {
        let functionKeyForAnonymousFunction =
            Coverage._getFunctionKeyFromReflectedFunction({
                id: null,
                loc: {
                  start: {
                      line: 2
                  }
                },
                params: ['a', 'b', 'c']
            });
        expect(functionKeyForAnonymousFunction).toEqual('(anonymous):2:3');
    });

    it('returns a function counter map for function keys', function () {
        let ast = {
            body: [{
                type: 'FunctionDeclaration',
                id: {
                    name: 'name'
                },
                loc: {
                  start: {
                      line: 1
                  }
                },
                params: [],
                body: {
                    type: 'BlockStatement',
                    body: []
                }
            }]
        };

        let detectedFunctions = Coverage.functionsForAST(ast);
        let functionCounters =
            Coverage._functionsToFunctionCounters('script', detectedFunctions);
        expect(functionCounters.name['1']['0'].hitCount).toEqual(0);
    });

    it('reports an error when two indistinguishable functions are present', function () {
        spyOn(window, 'log');
        let ast = {
            body: [{
                type: 'FunctionDeclaration',
                id: {
                    name: '(anonymous)'
                },
                loc: {
                  start: {
                      line: 1
                  }
                },
                params: [],
                body: {
                    type: 'BlockStatement',
                    body: []
                }
            }, {
                type: 'FunctionDeclaration',
                id: {
                    name: '(anonymous)'
                },
                loc: {
                  start: {
                      line: 1
                  }
                },
                params: [],
                body: {
                    type: 'BlockStatement',
                    body: []
                }
            }]
        };

        let detectedFunctions = Coverage.functionsForAST(ast);
        Coverage._functionsToFunctionCounters('script', detectedFunctions);

        expect(window.log).toHaveBeenCalledWith('script:1 Function ' +
            'identified as (anonymous):1:0 already seen in this file. ' +
            'Function coverage will be incomplete.');
    });

    it('populates a known functions array', function () {
        let functions = [
            { line: 1 },
            { line: 2 }
        ];

        let knownFunctionsArray = Coverage._populateKnownFunctions(functions, 4);

        expect(knownFunctionsArray)
            .toEqual([undefined, true, true, undefined, undefined]);
    });

    it('converts function counters to an array', function () {
        let functionsMap = {
            '(anonymous)': {
                '2': {
                    '0': {
                        hitCount: 1
                    },
                },
            },
            'name': {
                '1': {
                    '0': {
                        hitCount: 0
                    },
                },
            }
        };

        let expectedFunctionCountersArray = [
            jasmine.objectContaining({ name: '(anonymous):2:0', hitCount: 1 }),
            jasmine.objectContaining({ name: 'name:1:0', hitCount: 0 })
        ];

        let convertedFunctionCounters = Coverage._convertFunctionCountersToArray(functionsMap);

        expect(convertedFunctionCounters).toEqual(expectedFunctionCountersArray);
    });
});

describe('Coverage.incrementFunctionCounters', function () {
    it('increments for function on same execution start line', function () {
        let functionCounters = Coverage._functionsToFunctionCounters('script', [
            { key: 'f:1:0',
              line: 1,
              n_params: 0 }
        ]);
        Coverage._incrementFunctionCounters(functionCounters, null, 'f', 1, 0);

        expect(functionCounters.f['1']['0'].hitCount).toEqual(1);
    });

    it('can disambiguate two functions with the same name', function () {
        let functionCounters = Coverage._functionsToFunctionCounters('script', [
            { key: '(anonymous):1:0',
              line: 1,
              n_params: 0 },
            { key: '(anonymous):2:0',
              line: 2,
              n_params: 0 }
        ]);
        Coverage._incrementFunctionCounters(functionCounters, null, '(anonymous)', 1, 0);
        Coverage._incrementFunctionCounters(functionCounters, null, '(anonymous)', 2, 0);

        expect(functionCounters['(anonymous)']['1']['0'].hitCount).toEqual(1);
        expect(functionCounters['(anonymous)']['2']['0'].hitCount).toEqual(1);
    });

    it('can disambiguate two functions on same line with different params', function () {
        let functionCounters = Coverage._functionsToFunctionCounters('script', [
            { key: '(anonymous):1:0',
              line: 1,
              n_params: 0 },
            { key: '(anonymous):1:1',
              line: 1,
              n_params: 1 }
        ]);
        Coverage._incrementFunctionCounters(functionCounters, null, '(anonymous)', 1, 0);
        Coverage._incrementFunctionCounters(functionCounters, null, '(anonymous)', 1, 1);

        expect(functionCounters['(anonymous)']['1']['0'].hitCount).toEqual(1);
        expect(functionCounters['(anonymous)']['1']['1'].hitCount).toEqual(1);
    });

    it('can disambiguate two functions on same line by guessing closest params', function () {
        let functionCounters = Coverage._functionsToFunctionCounters('script', [
            { key: '(anonymous):1:0',
              line: 1,
              n_params: 0 },
            { key: '(anonymous):1:3',
              line: 1,
              n_params: 3 }
        ]);

        /* Eg, we called the function with 3 params with just two arguments. We
         * should be able to work out that we probably intended to call the
         * latter function as opposed to the former. */
        Coverage._incrementFunctionCounters(functionCounters, null, '(anonymous)', 1, 2);

        expect(functionCounters['(anonymous)']['1']['0'].hitCount).toEqual(0);
        expect(functionCounters['(anonymous)']['1']['3'].hitCount).toEqual(1);
    });

    it('increments for function on earlier start line', function () {
        let ast = {
            body: [{
                type: 'FunctionDeclaration',
                id: {
                    name: 'name'
                },
                loc: {
                  start: {
                      line: 1
                  }
                },
                params: [],
                body: {
                    type: 'BlockStatement',
                    body: []
                }
            }]
        };

        let detectedFunctions = Coverage.functionsForAST(ast);
        let knownFunctionsArray = Coverage._populateKnownFunctions(detectedFunctions, 3);
        let functionCounters = Coverage._functionsToFunctionCounters('script',
                                                                     detectedFunctions);

        /* We're entering at line two, but the function definition was actually
         * at line one */
        Coverage._incrementFunctionCounters(functionCounters, knownFunctionsArray, 'name', 2, 0);

        expect(functionCounters.name['1']['0'].hitCount).toEqual(1);
    });

    it('throws an error on unexpected function', function () {
        let ast = {
            body: [{
                type: 'FunctionDeclaration',
                id: {
                    name: 'name'
                },
                loc: {
                  start: {
                      line: 1
                  }
                },
                params: [],
                body: {
                    type: 'BlockStatement',
                    body: []
                }
            }]
        };
        let detectedFunctions = Coverage.functionsForAST(ast);
        let knownFunctionsArray = Coverage._populateKnownFunctions(detectedFunctions, 3);
        let functionCounters = Coverage._functionsToFunctionCounters('script',
                                                                     detectedFunctions);

        /* We're entering at line two, but the function definition was actually
         * at line one */
        expect(() => {
            Coverage._incrementFunctionCounters(functionCounters,
                                                knownFunctionsArray,
                                                'doesnotexist',
                                                2,
                                                0);
        }).toThrow();
    });

    it('throws if line out of range', function () {
        let expressionCounters = [
            undefined,
            0
        ];

        expect(() => {
            Coverage._incrementExpressionCounters(expressionCounters, 'script', 2);
        }).toThrow();
    });

    it('increments if in range', function () {
        let expressionCounters = [
            undefined,
            0
        ];

        Coverage._incrementExpressionCounters(expressionCounters, 'script', 1);
        expect(expressionCounters[1]).toEqual(1);
    });

    it('warns if we hit a non-executable line', function () {
        spyOn(window, 'log');
        let expressionCounters = [
            undefined,
            0,
            undefined
        ];

        Coverage._incrementExpressionCounters(expressionCounters, 'script', 2);

        expect(window.log).toHaveBeenCalledWith("script:2 Executed line " +
            "previously marked non-executable by Reflect");
        expect(expressionCounters[2]).toEqual(1);
    });
});

describe('Coverage statistics container', function () {
    const MockFiles = {
        'filename': "function f() {\n" +
                    "    return 1;\n" +
                    "}\n" +
                    "if (f())\n" +
                    "    f = 0;\n" +
                    "\n",
        'uncached': "function f() {\n" +
                    "    return 1;\n" +
                    "}\n"
    };

    const MockFilenames = Object.keys(MockFiles).concat(['nonexistent']);

    beforeEach(function () {
        Coverage.getFileContents =
            jasmine.createSpy('getFileContents').and.callFake(f => MockFiles[f]);
        Coverage.getFileChecksum =
            jasmine.createSpy('getFileChecksum').and.returnValue('abcd');
        Coverage.getFileModificationTime =
            jasmine.createSpy('getFileModificationTime').and.returnValue([1, 2]);
    });

    it('fetches valid statistics for file', function () {
        let container = new Coverage.CoverageStatisticsContainer(MockFilenames);

        let statistics = container.fetchStatistics('filename');
        expect(statistics).toBeDefined();

        let files = container.getCoveredFiles();
        expect(files).toEqual(['filename']);
    });

    it('throws for nonexisting file', function () {
        let container = new Coverage.CoverageStatisticsContainer(MockFilenames);
        expect(() => container.fetchStatistics('nonexistent')).toThrow();
    });

    const MockCache = '{ \
        "filename": { \
            "mtime": [1, 2], \
            "checksum": null, \
            "lines": [2, 4, 5], \
            "branches": [ \
                { \
                    "point": 4, \
                    "exits": [5] \
                } \
            ], \
            "functions": [ \
                { \
                    "key": "f:1:0", \
                    "line": 1 \
                } \
            ] \
        } \
    }';

    describe('with cache', function () {
        let container;
        beforeEach(function () {
            spyOn(Coverage, '_fetchCountersFromReflection').and.callThrough();
            container = new Coverage.CoverageStatisticsContainer(MockFilenames,
                                                                 MockCache);
        });

        it('fetches counters from cache', function () {
            container.fetchStatistics('filename');
            expect(Coverage._fetchCountersFromReflection).not.toHaveBeenCalled();
        });

        it('fetches counters from reflection if missed', function () {
            container.fetchStatistics('uncached');
            expect(Coverage._fetchCountersFromReflection).toHaveBeenCalled();
        });

        it('cache is not stale if all hit', function () {
            container.fetchStatistics('filename');
            expect(container.staleCache()).toBeFalsy();
        });

        it('cache is stale if missed', function () {
            container.fetchStatistics('uncached');
            expect(container.staleCache()).toBeTruthy();
        });
    });

    describe('coverage counters from cache', function () {
        let container, statistics;
        let containerWithNoCaching, statisticsWithNoCaching;
        beforeEach(function () {
            container = new Coverage.CoverageStatisticsContainer(MockFilenames,
                                                                 MockCache);
            statistics = container.fetchStatistics('filename');

            containerWithNoCaching = new Coverage.CoverageStatisticsContainer(MockFilenames);
            statisticsWithNoCaching = containerWithNoCaching.fetchStatistics('filename');
        });

        it('have same executable lines as reflection', function () {
            expect(statisticsWithNoCaching.expressionCounters)
                .toEqual(statistics.expressionCounters);
        });

        it('have same branch exits as reflection', function () {
            /* Branch starts on line 4 */
            expect(statisticsWithNoCaching.branchCounters[4].exits[0].line)
                .toEqual(statistics.branchCounters[4].exits[0].line);
        });

        it('have same branch points as reflection', function () {
            expect(statisticsWithNoCaching.branchCounters[4].point)
                .toEqual(statistics.branchCounters[4].point);
        });

        it('have same function keys as reflection', function () {
            /* Functions start on line 1 */
            expect(Object.keys(statisticsWithNoCaching.functionCounters))
                .toEqual(Object.keys(statistics.functionCounters));
        });
    });
});
