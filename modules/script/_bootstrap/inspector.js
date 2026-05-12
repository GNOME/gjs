/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global quit, loadNative */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Angelo Verlain

const {print} = loadNative('_print');

print('GJS inspector bootstrap loaded');
quit(0);
