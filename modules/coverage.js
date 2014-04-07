/*
 * Copyright (c) 2014 Endless Mobile, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */

function getSubNodesForNode(node) {
    let subNodes = [];
    switch (node.type) {
    /* These statements have a single body */
    case 'LabelledStatement':
    case 'WithStatement':
    case 'LetStatement':
    case 'ForInStatement':
    case 'ForOfStatement':
    case 'FunctionDeclaration':
    case 'FunctionExpression':
    case 'ArrowExpression':
    case 'CatchClause':
        subNodes.push(node.body);
        break;
    case 'WhileStatement':
    case 'DoWhileStatement':
        subNodes.push(node.body);
        subNodes.push(node.test);
        break;
    case 'ForStatement':
        if (node.init !== null)
            subNodes.push(node.init);
        if (node.test !== null)
            subNodes.push(node.test);
        if (node.update !== null)
            subNodes.push(node.update);

        subNodes.push(node.body);
        break;
    case 'BlockStatement':
        Array.prototype.push.apply(subNodes, node.body);
        break;
    case 'ThrowStatement':
    case 'ReturnStatement':
        if (node.argument !== null)
            subNodes.push(node.argument);
        break;
    case 'ExpressionStatement':
        subNodes.push(node.expression);
        break;
    case 'AssignmentExpression':
        subNodes.push(node.left, node.right);
        break;
    case 'ObjectExpression':
        node.properties.forEach(function(prop) {
            subNodes.push(prop.value);
        });
        break;
    /* It is very possible that there might be something
     * interesting in the function arguments, so we need to
     * walk them too */
    case 'NewExpression':
    case 'CallExpression':
        Array.prototype.push.apply(subNodes, node.arguments);
        subNodes.push(node.callee);
        break;
    /* These statements might have multiple different bodies
     * depending on whether or not they were entered */
    case 'IfStatement':
        subNodes = [node.test, node.consequent];
        if (node.alternate !== null)
            subNodes.push(node.alternate);
        break;
    case 'TryStatement':
        subNodes = [node.block];
        if (node.handler !== null)
            subNodes.push(node.handler);
        if (node.finalizer !== null)
            subNodes.push(node.finalizer);
        break;
    case 'SwitchStatement':
        for (let caseClause of node.cases) {
            caseClause.consequent.forEach(function(expression) {
                subNodes.push(expression);
            });
        }

        break;
    /* Variable declarations might be initialized to
     * some expression, so traverse the tree and see if
     * we can get into the expression */
    case 'VariableDeclaration':
        node.declarations.forEach(function (declarator) {
            if (declarator.init !== null)
                subNodes.push(declarator.init);
        });

        break;
    }

    return subNodes;
}

function collectForSubNodes(subNodes, collector) {
    let result = [];
    if (subNodes !== undefined &&
        subNodes.length > 0) {

        subNodes.forEach(function(node) {
            let nodeResult = collector(node);
            if (nodeResult !== undefined)
                Array.prototype.push.apply(result, nodeResult);

            let subNodeResults = collectForSubNodes(getSubNodesForNode(node),
                                                    collector);

            Array.prototype.push.apply(result, subNodeResults);
        });
    }

    return result;
}

/* Unfortunately, the Reflect API doesn't give us enough information to
 * uniquely identify a function. A function might be anonymous, in which
 * case the JS engine uses some heurisitics to get a unique string identifier
 * but that isn't available to us here.
 *
 * There's also the edge-case where functions with the same name might be
 * defined within the same scope, or multiple anonymous functions might
 * be defined on the same line. In that case, it will look like we entered
 * the same function multiple times since we can't get column information
 * from the engine-side.
 *
 * For instance:
 *
 * 1. function f() {
 *       function f() {
 *       }
 *    }
 *
 * 2. let a = function() { function(a, b) {} };
 *
 * 3. let a = function() { function () {} }
 *
 * We can work-around case 1 by using the line numbers to get a unique identifier.
 * We can work-around case 2 by using the arguments length to get a unique identifier
 * We can't work-around case 3. The best thing we can do is warn that coverage
 * reports might be inaccurate as a result */
function functionsForNode(node) {
    let functionNames = [];
    switch (node.type) {
    case 'FunctionDeclaration':
    case 'FunctionExpression':
        if (node.id !== null) {
            functionNames.push({ name: node.id.name,
                                 line: node.loc.start.line,
                                 n_params: node.params.length });
        }
        /* If the function wasn't found, we just push a name
         * that looks like 'function:lineno' to signify that
         * this was an anonymous function. If the coverage tool
         * enters a function with no name (but a line number)
         * then it can probably use this information to
         * figure out which function it was */
        else {
            functionNames.push({ name: null,
                                 line: node.loc.start.line,
                                 n_params: node.params.length });
        }
    }

    return functionNames;
}

function functionsForAST(ast) {
    return collectForSubNodes(ast.body, functionsForNode);
}

/* If a branch' consequent is a block statement, there's
 * a chance that it could start on the same line, although
 * that's not where execution really starts. If it is
 * a block statement then handle the case and go
 * to the first line where execution starts */
function getBranchExitStartLine(branchBodyNode) {
    switch (branchBodyNode.type) {
    case 'BlockStatement':
        /* Hit a block statement, but nothing inside, can never
         * be executed, tell the upper level to move on to the next
         * statement */
        if (branchBodyNode.body.length === 0)
            return -1;

        /* Handle the case where we have nested block statements
         * that never actually get to executable code by handling
         * all statements within a block */
        for (let statement of branchBodyNode.body) {
            let startLine = getBranchExitStartLine(statement);
            if (startLine !== -1)
                return startLine;
        }

        /* Couldn't find an executable line inside this block */
        return -1;

    case 'SwitchCase':
        /* Hit a switch, but nothing inside, can never
         * be executed, tell the upper level to move on to the next
         * statement */
        if (branchBodyNode.consequent.length === 0)
            return -1;

        /* Handle the case where we have nested block statements
         * that never actually get to executable code by handling
         * all statements within a block */
        for (let statement of branchBodyNode.consequent) {
            let startLine = getBranchExitStartLine(statement);
            if (startLine !== -1) {
                return startLine;
            }
        }

        /* Couldn't find an executable line inside this block */
        return -1;
    /* These types of statements are never executable */
    case 'EmptyStatement':
    case 'LabelledStatement':
        return -1;
    default:
        break;
    }

    return branchBodyNode.loc.start.line;
}

function branchesForNode(node) {
    let branches = [];

    let branchExitNodes = [];
    switch (node.type) {
    case 'IfStatement':
        branchExitNodes.push(node.consequent);
        if (node.alternate !== null)
            branchExitNodes.push(node.alternate);
        break;
    case 'WhileStatement':
    case 'DoWhileStatement':
        branchExitNodes.push(node.body);
        break;
    case 'SwitchStatement':

        /* The case clauses by themselves are never executable
         * so find the actual exits */
        Array.prototype.push.apply(branchExitNodes, node.cases);
        break;
    default:
        break;
    }

    let branchExitStartLines = branchExitNodes.map(getBranchExitStartLine);
    branchExitStartLines = branchExitStartLines.filter(function(line) {
        return line !== -1;
    });

    /* Branch must have at least one exit */
    if (branchExitStartLines.length) {
        branches.push({ point: node.loc.start.line,
                        exits: branchExitStartLines });
    }

    return branches;
}

function branchesForAST(ast) {
    return collectForSubNodes(ast.body, branchesForNode);
}

function expressionLinesForNode(statement) {
    let expressionLines = [];

    let expressionNodeTypes = ['Expression',
                               'Declaration',
                               'Statement',
                               'Clause',
                               'Literal',
                               'Identifier'];

    if (expressionNodeTypes.some(function(type) {
            return statement.type.indexOf(type) !== -1;
        })) {

        /* These expressions aren't executable on their own */
        switch (statement.type) {
        case 'FunctionDeclaration':
        case 'LiteralExpression':
            break;
        /* Perplexingly, an empty block statement is actually executable,
         * push it if it is */
        case 'BlockStatement':
            if (statement.body.length !== 0)
                break;
            expressionLines.push(statement.loc.start.line);
            break;
        default:
            expressionLines.push(statement.loc.start.line);
            break;
        }
    }

    return expressionLines;
}

function deduplicate(list) {
    return list.filter(function(elem, pos, self) {
        return self.indexOf(elem) === pos;
    });
}

function expressionLinesForAST(ast) {
    let allExpressions = collectForSubNodes(ast.body, expressionLinesForNode);
    allExpressions = deduplicate(allExpressions);

    return allExpressions;
}

function _getNumberOfLinesForScript(scriptContents) {
    let scriptLines = scriptContents.split("\n");
    let scriptLineCount = scriptLines.length;

    return scriptLineCount;
}

/*
 * The created array is a 1-1 representation of the hitcount in the filename. Each
 * element refers to an individual line. In order to avoid confusion, our array
 * is zero indexed, but the zero'th line is always ignored and the first element
 * refers to the first line of the file.
 *
 * A value of undefined for an element means that the line is non-executable and never actually
 * reached. A value of 0 means that it was executable but never reached. A positive value
 * indicates the hit count.
 *
 * We care about non-executable lines because we don't want to report coverage misses for
 * lines that could have never been executed anyways.
 *
 * The reason for using a 1-1 mapping as opposed to an array of key-value pairs for executable
 * lines is:
 *   1. Lookup speed is O(1) instead of O(log(n))
 *   2. There's a possibility we might hit a line which we thought was non-executable, in which
 *      case we can neatly handle the error by marking that line executable. A hit on a line
 *      we thought was non-executable is not as much of a problem as noise generated by
 *      ostensible "misses" which could in fact never be executed.
 *
 */
function _expressionLinesToCounters(expressionLines, nLines) {
    expressionLines.sort(function(left, right) { return left - right; });

    let expressionLinesIndex = 0;
    let counters = new Array(nLines);

    if (expressionLines.length === 0)
        return counters;

    for (let i = 1; i < counters.length; i++) {
        if (expressionLines[expressionLinesIndex] == i) {
            counters[i] = 0;
            expressionLinesIndex++;
        }
    }

    return counters;
}

/* As above, we are creating a 1-1 representation of script lines to potential branches
 * where each element refers to a 1-index line (with the zero'th ignored).
 *
 * Each element is a GjsCoverageBranchData which, if the line at the element
 * position describes a branch, will be populated with a GjsReflectedScriptBranchInfo
 * and an array of unsigned each specifying the hit-count for each potential branch
 * in the branch info */
function _branchesToBranchCounters(branches, nLines) {
    branches.sort(function(left, right) {
        return left.point - right.point;
    });

    let branchIndex = 0;
    let counters = new Array(nLines);

    if (branches.length === 0)
        return counters;

    for (let i = 1; i < counters.length; i++) {
        let branch = branches[branchIndex];
        let branchPoint = branch.point;

        if (branchPoint == i) {
            counters[i] = {
                point: branchPoint,
                exits: branch.exits.map(function(exit) {
                    return {
                        line: exit,
                        hitCount: 0
                    };
                }),
                lastExit: (function() {
                    let lastExitLine = 0;
                    for (let exit of branch.exits) {
                        if (lastExitLine < exit)
                            lastExitLine = exit;
                    }

                    return lastExitLine;
                })(),
                hit: false
            };

            if (++branchIndex >= branches.length)
                break;
        }
    }

    return counters;
}

function _getFunctionKeyFromReflectedFunction(func) {
    let name = func.name !== null ? func.name : '(anonymous)';
    let line = func.line;
    let n_params = func.n_params;

    return name + ':' + line + ':' + n_params;
}

function _functionsToFunctionCounters(functions) {
    let functionCounters = {};

    functions.forEach(function(func) {
        let functionKey = _getFunctionKeyFromReflectedFunction(func);
        functionCounters[functionKey] = {
            hitCount: 0
        };
    });

    return functionCounters;
}

function _populateKnownFunctions(functions, nLines) {
    let knownFunctions = new Array(nLines);

    functions.forEach(function(func) {
        knownFunctions[func.line] = true;
    });

    return knownFunctions;
}

/**
 * _incrementFunctionCounters
 *
 * functionCounters: An object which is a key-value pair with the following schema:
 * {
 *      "key" : { line, hitCount }
 * }
 * linesWithKnownFunctions: An array of either "true" or undefined, with true set to
 * each element corresponding to a line that we know has a function on it.
 * name: The name of the function or "(anonymous)" if it has no name
 * line: The line at which execution first started on this function.
 * nArgs: The number of arguments this function has.
 */
function _incrementFunctionCounters(functionCounters,
                                    linesWithKnownFunctions,
                                    name,
                                    line,
                                    nArgs) {
    let functionKey = name + ':' + line + ':' + nArgs;
    let functionCountersForKey = functionCounters[functionKey];

    /* Its possible that the JS Engine might enter a funciton
     * at an executable line which is a little bit past the
     * actual definition. Roll backwards until we reach the
     * last known function definition line which we kept
     * track of earlier to see if we can find this function first */
    if (functionCountersForKey === undefined) {
        do {
            --line;
            functionKey = name + ':' + line + ':' + nArgs;
            functionCountersForKey = functionCounters[functionKey];
        } while(linesWithKnownFunctions[line] !== true)
    }

    if (functionCountersForKey !== undefined) {
        functionCountersForKey.hitCount++;
    } else {
        throw new Error("expected Reflect to find function " + functionKey);
    }
}

/**
 * _incrementExpressionCounters
 *
 * expressonCounters: An array of either a hit count for a found
 * executable line or undefined for a known non-executable line.
 * line: an executed line
 * reporter: A function a single integer to report back when
 * we executed lines that we didn't expect
 */
function _incrementExpressionCounters(expressionCounters,
                                      offsetLine,
                                      reporter) {
    let expressionCountersLen = expressionCounters.length;

    if (offsetLine >= expressionCountersLen)
        throw new Error("Executed line " + offsetLine + " which was past the highest-found line " + expressionCountersLen);

    /* If this happens it is not a huge problem - though it does
     * mean that the reflection machinery is not doing its job, so we should
     * print a debug message about it in case someone is interested.
     *
     * The reason why we don't have a proper warning is because it
     * is difficult to determine what the SpiderMonkey program counter
     * will actually pass over, especially function declarations for some
     * reason:
     *
     *     function f(a,b) {
     *         a = 1;
     *     }
     *
     * In some cases, the declaration itself will be executed
     * but in other cases it won't be. Reflect.parse tells us that
     * the only two expressions on that line are a FunctionDeclaration
     * and BlockStatement, neither of which would ordinarily be
     * executed */
    if (expressionCounters[offsetLine] === undefined) {
        if (reporter !== undefined)
            reporter(offsetLine);

        expressionCounters[offsetLine] = 0;
    }

    expressionCounters[offsetLine]++;
}

function _BranchTracker(branchCounters) {
    this._branchCounters = branchCounters;
    this._activeBranch = undefined;

    this.incrementBranchCounters = function(offsetLine) {
        /* Set branch exits or find a new active branch */
        let activeBranch = this._activeBranch;
        if (activeBranch !== undefined) {
            activeBranch.exits.forEach(function(exit) {
                if (exit.line === offsetLine) {
                    exit.hitCount++;
                }
            });

            /* Only set the active branch to undefined once we're
             * completely outside of it, since we might be in a case statement where
             * we need to check every possible option before jumping to an
             * exit */
            if (offsetLine >= activeBranch.lastExit)
                this._activeBranch = undefined;
        }

        let nextActiveBranch = branchCounters[offsetLine];
        if (nextActiveBranch !== undefined) {
            this._activeBranch = nextActiveBranch;
            this._activeBranch.hit = true;
        }
    };
}

function _convertFunctionCountersToArray(functionCounters) {
    let arrayReturn = [];
    /* functionCounters is an object so convert it to
     * an array-of-object using the key as a property
     * of that object */
    for (let key in functionCounters) {
        let func = functionCounters[key];
        arrayReturn.push({ name: key,
                           hitCount: func.hitCount });
    }

    arrayReturn.sort(function(left, right) {
        if (left.name < right.name)
            return -1;
        else if (left.name > right.name)
            return 1;
        else
            return 0;
    });
    return arrayReturn;
}

function CoverageStatisticsContainer(files) {
    let pendingFiles = files;
    let coveredFiles = {};

    function wantsStatisticsFor(filename) {
        return pendingFiles.indexOf(filename) !== -1;
    }

    function createStatisticsFor(filename) {
        let idx = pendingFiles.indexOf(filename);
        pendingFiles.splice(idx, 1);

        let contents = getFileContents(filename);
        let reflection = Reflect.parse(contents);
        let nLines = _getNumberOfLinesForScript(contents);

        let functions = functionsForAST(reflection);

        return {
            contents: contents,
            nLines: nLines,
            expressionCounters: _expressionLinesToCounters(expressionLinesForAST(reflection), nLines),
            branchCounters: _branchesToBranchCounters(branchesForAST(reflection), nLines),
            functionCounters: _functionsToFunctionCounters(functions),
            linesWithKnownFunctions: _populateKnownFunctions(functions, nLines)
        };
    }

    function ensureStatisticsFor(filename) {
        if (!coveredFiles[filename] && wantsStatisticsFor(filename))
            coveredFiles[filename] = createStatisticsFor(filename);
        return coveredFiles[filename];
    }

    this.fetchStatistics = function(filename) {
        let statistics = ensureStatisticsFor(filename);
        if (statistics === undefined)
            throw new Error('Not tracking statistics for ' + filename);
        return statistics;
    };
}

/**
 * Main class tying together the Debugger object and CoverageStatisticsContainer.
 *
 * It isn't poissible to unit test this class because it depends on running
 * Debugger which in turn depends on objects injected in from another compartment */
function CoverageStatistics(files) {
    this.container = new CoverageStatisticsContainer(files);
    let fetchStatistics = this.container.fetchStatistics.bind(this.container);

    /* 'debuggee' comes from the invocation from
     * a separate compartment inside of coverage.cpp */
    this.dbg = new Debugger(debuggee);

    this.getNumberOfLinesFor = function(filename) {
        return fetchStatistics(filename).nLines;
    };

    this.getExecutedLinesFor = function(filename) {
        return fetchStatistics(filename).expressionCounters;
    };

    this.getBranchesFor = function(filename) {
        return fetchStatistics(filename).branchCounters;
    };

    this.getFunctionsFor = function(filename) {
        let functionCounters = fetchStatistics(filename).functionCounters;
        return _convertFunctionCountersToArray(functionCounters);
    };

    this.dbg.onEnterFrame = function(frame) {
        let statistics;

        try {
            statistics = fetchStatistics(frame.script.url);
        } catch (e) {
            /* We don't care about this frame, return */
            return undefined;
        }

        /* Log function calls */
        if (frame.callee !== null && frame.callee.callable) {
            let name = frame.callee.name ? frame.callee.name : "(anonymous)";
            let line = frame.script.getOffsetLine(frame.offset);
            let nArgs = frame.callee.parameterNames.length;

            _incrementFunctionCounters(statistics.functionCounters,
                                       statistics.linesWithKnownFunctions,
                                       name,
                                       line,
                                       nArgs);
        }

        /* Upon entering the frame, the active branch is always inactive */
        frame._branchTracker = new _BranchTracker(statistics.branchCounters);

        /* Set single-step hook */
        frame.onStep = function() {
            /* Line counts */
            let offset = this.offset;
            let offsetLine = this.script.getOffsetLine(offset);

            _incrementExpressionCounters(statistics.expressionCounters,
                                         offsetLine,
                                         function(line) {
                                             warning("executed " +
                                                     frame.script.url +
                                                     ":" +
                                                     offsetLine +
                                                     " which we thought wasn't executable");
                                         });

            this._branchTracker.incrementBranchCounters(offsetLine);
        };

        return undefined;
    };
}

