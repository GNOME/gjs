/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, getSourceMapRegistry */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2026 Angelo Verlain

"use strict";

const { print } = loadNative("_print");

print("GJS inspector bootstrap loaded");
quit(0);
