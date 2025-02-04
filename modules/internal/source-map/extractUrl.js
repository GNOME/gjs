// SPDX-License-Identifier: CC-BY-3.0
// SPDX-FileCopyrightText: 2024 Source Maps Task Group (TC39-TG4)
// Reference implementation in Source Map V3 Standard 6.2.2.1
// @ts-nocheck
export function extractUrl(source) {
    const JS_NEWLINE = /^/m;

    // This RegExp will always match one of the following:
    // - single-line comments
    // - "single-line" multi-line comments
    // - unclosed multi-line comments
    // - just trailing whitespaces
    // - a code character
    // The loop below differentiates between all these cases.
    const JS_COMMENT = /\s*(?:\/\/(?<single>.*)|\/\*(?<multi>.*?)\*\/|\/\*.*|$|(?<code>[^\/]+))/muy;

    const PATTERN = /^[@#]\s*sourceMappingURL=(\S*?)\s*$/;

    let lastURL = null;
    for (const line of source.split(JS_NEWLINE)) {
        JS_COMMENT.lastIndex = 0;
        while (JS_COMMENT.lastIndex < line.length) {
            const exec = JS_COMMENT.exec(line);
            if (!exec) break; // added
            let commentMatch = exec.groups;
            let comment = commentMatch.single ?? commentMatch.multi;
            if (comment != null) {
                let match = PATTERN.exec(comment);
                if (match !== null) lastURL = match[1];
            } else if (commentMatch.code != null) {
                lastURL = null;
            } else {
                // We found either trailing whitespaces or an unclosed comment.
                // Assert: JS_COMMENT.lastIndex === line.length
            }
        }
    }
    return lastURL;
}
