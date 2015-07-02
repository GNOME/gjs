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
    case 'FunctionDeclaration':
    case 'FunctionExpression':
    case 'CatchClause':
        subNodes.push(node.body);
        break;
    case 'LetStatement':
        Array.prototype.push.apply(subNodes, node.head);
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
    case 'ForInStatement':
        if (node.each)
            subNodes.push(node.left);

        subNodes.push(node.right, node.body);
        break;
    case 'ForOfStatement':
        subNodes.push(node.left, node.right, node.body);
        break;
    case 'BlockStatement':
        Array.prototype.push.apply(subNodes, node.body);
        break;
    case 'ThrowStatement':
    case 'ReturnStatement':
    case 'YieldExpression':
        if (node.argument !== null)
            subNodes.push(node.argument);
        break;
    case 'ExpressionStatement':
        subNodes.push(node.expression);
        break;
    case 'AssignmentExpression':
    case 'BinaryExpression':
    case 'LogicalExpression':
        subNodes.push(node.left, node.right);
        break;
    case 'ConditionalExpression':
        subNodes.push(node.test, node.consequent, node.alternate);
        break;
    case 'ObjectExpression':
        node.properties.forEach(function(prop) {
            subNodes.push(prop.value);
        });
        break;
    case 'ArrayExpression':
        node.elements.forEach(function(elem) {
            if (elem !== null)
                subNodes.push(elem);
        });
        break;
    case 'ArrowExpression':
        Array.prototype.push.apply(subNodes, node.defaults);
        subNodes.push(node.body);
        break;
    case 'SequenceExpression':
        Array.prototype.push.apply(subNodes, node.expressions);
        break;
    case 'UnaryExpression':
    case 'UpdateExpression':
        subNodes.push(node.argument);
        break;
    case 'ComprehensionExpression':
    case 'GeneratorExpression':
        subNodes.push(node.body);
        Array.prototype.push.apply(subNodes, node.blocks);
        if (node.filter !== null)
            subNodes.push(node.filter);
        break;
    case 'ComprehensionBlock':
        subNodes.push(node.right);
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
    case 'VariableDeclaration':
        Array.prototype.push.apply(subNodes, node.declarations);
        break;
    case 'VariableDeclarator':
        if (node.init !== null)
            subNodes.push(node.init);
        break;
    case 'MemberExpression':
        subNodes.push(node.object);
        if (node.computed)
            subNodes.push(node.property);

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

function _getFunctionKeyFromReflectedFunction(node) {
    let name = node.id !== null ? node.id.name : '(anonymous)';
    let line = node.loc.start.line;
    let n_params = node.params.length;

    return [name, line, n_params].join(':');
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
    case 'ArrowExpression':
        functionNames.push({ key: _getFunctionKeyFromReflectedFunction(node),
                             line: node.loc.start.line,
                             n_params: node.params.length });
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
    let counters = new Array(nLines + 1);

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
    let counters = new Array(nLines + 1);

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

function _functionsToFunctionCounters(script, functions) {
    let functionCounters = {};

    functions.forEach(function(func) {
        let [name, line, args] = func.key.split(':');

        if (functionCounters[name] === undefined) {
            functionCounters[name] = {};
        }

        if (functionCounters[name][line] === undefined) {
            functionCounters[name][line] = {};
        }

        if (functionCounters[name][line][args] === undefined) {
            functionCounters[name][line][args] = { hitCount: 0 };
        } else {
            log(script + ':' + line + ' Function identified as ' +
                func.key + ' already seen in this file. Function coverage ' +
                'will be incomplete.');
        }
    });

    return functionCounters;
}

function _populateKnownFunctions(functions, nLines) {
    let knownFunctions = new Array(nLines + 1);

    functions.forEach(function(func) {
        knownFunctions[func.line] = true;
    });

    return knownFunctions;
}

function _identifyFunctionCounterInLinePartForDescription(linePart,
                                                          nArgs) {
    /* There is only one potential option for this line. We might have been
     * called with the wrong number of arguments, but it doesn't matter. */
    if (Object.getOwnPropertyNames(linePart).length === 1)
        return linePart[Object.getOwnPropertyNames(linePart)[0]];

    /* Have to disambiguate using nArgs and we have an exact match. */
    if (linePart[nArgs] !== undefined)
        return linePart[nArgs];

    /* There are multiple options on this line. Pick the one where
     * we satisfy the number of arguments exactly, or failing that,
     * go through each and pick the closest. */
    let allNArgsOptions = Object.keys(linePart).map(function(key) {
        return parseInt(key);
    });

    let closest = allNArgsOptions.reduce(function(prev, current, index, array) {
        let nArgsOption = array[index];
        if (Math.abs(nArgsOption - nArgs) < Math.abs(current - nArgs)) {
            return nArgsOption;
        }

        return current;
    });

    return linePart[String(closest)];
}

function _identifyFunctionCounterForDescription(functionCounters,
                                                name,
                                                line,
                                                nArgs) {
    let candidateCounter = functionCounters[name];

    if (candidateCounter === undefined)
        return null;

    if (Object.getOwnPropertyNames(candidateCounter).length === 1) {
        let linePart = candidateCounter[Object.getOwnPropertyNames(candidateCounter)[0]];
        return _identifyFunctionCounterInLinePartForDescription(linePart, nArgs);
    }

    let linePart = functionCounters[name][line];

    if (linePart === undefined) {
        return null;
    }

    return _identifyFunctionCounterInLinePartForDescription(linePart, nArgs);
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
    let functionCountersForKey = _identifyFunctionCounterForDescription(functionCounters,
                                                                        name,
                                                                        line,
                                                                        nArgs);

    /* Its possible that the JS Engine might enter a funciton
     * at an executable line which is a little bit past the
     * actual definition. Roll backwards until we reach the
     * last known function definition line which we kept
     * track of earlier to see if we can find this function first */
    if (functionCountersForKey === null) {
        do {
            --line;
            functionCountersForKey = _identifyFunctionCounterForDescription(functionCounters,
                                                                            name,
                                                                            line,
                                                                            nArgs);
        } while (linesWithKnownFunctions[line] !== true && line > 0);
    }

    if (functionCountersForKey !== null) {
        functionCountersForKey.hitCount++;
    } else {
        let functionKey = [name, line, nArgs].join(':');
        throw new Error("expected Reflect to find function " + functionKey);
    }
}

/**
 * _incrementExpressionCounters
 *
 * expressonCounters: An array of either a hit count for a found
 * executable line or undefined for a known non-executable line.
 * line: an executed line
 */
function _incrementExpressionCounters(expressionCounters,
                                      script,
                                      offsetLine) {
    let expressionCountersLen = expressionCounters.length;

    if (offsetLine >= expressionCountersLen)
        throw new Error("Executed line " + offsetLine + " which was past the highest-found line " + expressionCountersLen);

    /* If this happens it is not a huge problem - though it does
     * mean that the reflection machinery is not doing its job, so we should
     * print a debug message about it in case someone is interested.
     *
     * The reason why we don't have a proper log is because it
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
        log(script + ':' + offsetLine + ' Executed line previously marked ' +
            'non-executable by Reflect');
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
    /* functionCounters is an object so explore it to create a
     * set of function keys and then convert it to
     * an array-of-object using the key as a property
     * of that object */
    for (let name of Object.getOwnPropertyNames(functionCounters)) {
        let namePart = functionCounters[name];

        for (let line of Object.getOwnPropertyNames(namePart)) {
            let linePart = functionCounters[name][line];

            for (let nArgs of Object.getOwnPropertyNames(linePart)) {
                let functionKey = [name, line, nArgs].join(':');
                arrayReturn.push({ name: functionKey,
                                   line: Number(line),
                                   nArgs: nArgs,
                                   hitCount: linePart[nArgs].hitCount });
            }
        }
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

/* Looks up filename in cache and fetches statistics
 * directly from the cache */
function _fetchCountersFromCache(filename, cache, nLines) {
    if (!cache)
        return null;

    if (Object.keys(cache).indexOf(filename) !== -1) {
        let cache_for_file = cache[filename];

        if (cache_for_file.mtime) {
             let mtime = getFileModificationTime(filename);
             if (mtime[0] != cache[filename].mtime[0] ||
                 mtime[1] != cache[filename].mtime[1])
                 return null;
        } else {
            let checksum = getFileChecksum(filename);
            if (checksum != cache[filename].checksum)
                return null;
        }

        let functions = cache_for_file.functions;

        return {
            expressionCounters: _expressionLinesToCounters(cache_for_file.lines, nLines),
            branchCounters: _branchesToBranchCounters(cache_for_file.branches, nLines),
            functionCounters: _functionsToFunctionCounters(filename, functions),
            linesWithKnownFunctions: _populateKnownFunctions(functions, nLines),
            nLines: nLines
        };
    }

    return null;
}

function _fetchCountersFromReflection(filename, contents, nLines) {
    let reflection = Reflect.parse(contents);
    let functions = functionsForAST(reflection);

    return {
        expressionCounters: _expressionLinesToCounters(expressionLinesForAST(reflection), nLines),
        branchCounters: _branchesToBranchCounters(branchesForAST(reflection), nLines),
        functionCounters: _functionsToFunctionCounters(filename, functions),
        linesWithKnownFunctions: _populateKnownFunctions(functions, nLines),
        nLines: nLines
    };
}

function CoverageStatisticsContainer(prefixes, cache) {
    /* Copy the files array, so that it can be re-used in the tests */
    let cachedASTs = cache !== undefined ? JSON.parse(cache) : null;
    let coveredFiles = {};
    let cacheMisses = 0;

    function wantsStatisticsFor(filename) {
        return prefixes.some(function(prefix) {
            return filename.startsWith(prefix);
        });
    }

    function createStatisticsFor(filename) {
        let contents = getFileContents(filename);
        let nLines = _getNumberOfLinesForScript(contents);

        let counters = _fetchCountersFromCache(filename, cachedASTs, nLines);
        if (counters === null) {
            cacheMisses++;
            counters = _fetchCountersFromReflection(filename, contents, nLines);
        }

        if (counters === null)
            throw new Error('Failed to parse and reflect file ' + filename);

        /* Set contents here as we don't pass it to _fetchCountersFromCache. */
        counters.contents = contents;

        return counters;
    }

    function ensureStatisticsFor(filename) {
        let wantStatistics = wantsStatisticsFor(filename);
        let haveStatistics = !!coveredFiles[filename];

        if (wantStatistics && !haveStatistics)
            coveredFiles[filename] = createStatisticsFor(filename);
        return coveredFiles[filename];
    }

    this.stringify = function() {
        let cache_data = {};
        Object.keys(coveredFiles).forEach(function(filename) {
            let statisticsForFilename = coveredFiles[filename];
            let mtime = getFileModificationTime(filename);
            let cacheDataForFilename = {
                mtime: mtime,
                checksum: mtime === null ? getFileChecksum(filename) : null,
                lines: [],
                branches: [],
                functions: _convertFunctionCountersToArray(statisticsForFilename.functionCounters).map(function(func) {
                    return {
                        key: func.name,
                        line: func.line
                    };
                })
            };

            /* We're using a index based loop here since we need access to the
             * index, since it actually represents the current line number
             * on the file (see _expressionLinesToCounters). */
            for (let line_index = 0;
                 line_index < statisticsForFilename.expressionCounters.length;
                 ++line_index) {
                 if (statisticsForFilename.expressionCounters[line_index] !== undefined)
                     cacheDataForFilename.lines.push(line_index);

                 if (statisticsForFilename.branchCounters[line_index] !== undefined) {
                     let branchCounters = statisticsForFilename.branchCounters[line_index];
                     cacheDataForFilename.branches.push({
                         point: statisticsForFilename.branchCounters[line_index].point,
                         exits: statisticsForFilename.branchCounters[line_index].exits.map(function(exit) {
                             return exit.line;
                         })
                     });
                 }
            }
            cache_data[filename] = cacheDataForFilename;
        });
        return JSON.stringify(cache_data);
    };

    this.getCoveredFiles = function() {
        return Object.keys(coveredFiles);
    };

    this.fetchStatistics = function(filename) {
        return ensureStatisticsFor(filename);
    };

    this.staleCache = function() {
        return cacheMisses > 0;
    };

    this.deleteStatistics = function(filename) {
        coveredFiles[filename] = undefined;
    };
}

/**
 * Main class tying together the Debugger object and CoverageStatisticsContainer.
 *
 * It isn't poissible to unit test this class because it depends on running
 * Debugger which in turn depends on objects injected in from another compartment */
function CoverageStatistics(prefixes, cache) {
    this.container = new CoverageStatisticsContainer(prefixes, cache);
    let fetchStatistics = this.container.fetchStatistics.bind(this.container);
    let deleteStatistics = this.container.deleteStatistics.bind(this.container);

    /* 'debuggee' comes from the invocation from
     * a separate compartment inside of coverage.cpp */
    this.dbg = new Debugger(debuggee);

    this.getCoveredFiles = function() {
        return this.container.getCoveredFiles();
    };

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
            if (!statistics) {
                return undefined;
            }
        } catch (e) {
            /* We don't care about this frame, return */
            log(e.message + " " + e.stack);
            return undefined;
        }

        function _logExceptionAndReset(exception, callee, line) {
            log(exception.fileName + ":" + exception.lineNumber +
                " (processing " + frame.script.url + ":" + callee + ":" +
                line + ") - " + exception.message);
            log("Will not log statistics for this file");
            frame.onStep = undefined;
            frame._branchTracker = undefined;
            deleteStatistics(frame.script.url);
        }

        /* Log function calls */
        if (frame.callee !== null && frame.callee.callable) {
            let name = frame.callee.name ? frame.callee.name : "(anonymous)";
            let line = frame.script.getOffsetLine(frame.offset);
            let nArgs = frame.callee.parameterNames.length;

            try {
                _incrementFunctionCounters(statistics.functionCounters,
                                           statistics.linesWithKnownFunctions,
                                           name,
                                           line,
                                           nArgs);
            } catch (e) {
                /* Something bad happened. Log the exception and delete
                 * statistics for this file */
                _logExceptionAndReset(e, name, line);
                return undefined;
            }
        }

        /* Upon entering the frame, the active branch is always inactive */
        frame._branchTracker = new _BranchTracker(statistics.branchCounters);

        /* Set single-step hook */
        frame.onStep = function() {
            /* Line counts */
            let offset = this.offset;
            let offsetLine = this.script.getOffsetLine(offset);

            try {
                _incrementExpressionCounters(statistics.expressionCounters,
                                             frame.script.url,
                                             offsetLine);
                this._branchTracker.incrementBranchCounters(offsetLine);
            } catch (e) {
                /* Something bad happened. Log the exception and delete
                 * statistics for this file */
                _logExceptionAndReset(e, frame.callee, offsetLine);
            }
        };

        return undefined;
    };

    this.deactivate = function() {
        /* This property is designed to be a one-stop-shop to
         * disable the debugger for this debugee, without having
         * to traverse all its scripts or frames */
        this.dbg.enabled = false;
    };

    this.staleCache = this.container.staleCache.bind(this.container);
    this.stringify = this.container.stringify.bind(this.container);
}
