// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Node.js contributors. All rights reserved.

import {CSI} from './utils.js';

const {Number} = globalThis;

const NumberIsNaN = Number.isNaN;

// Adapted from https://github.com/nodejs/node/blob/1b550ba1af50a9e7eed9b27a92902115f98cf4d8/lib/internal/readline/callbacks.js

/* eslint-disable */

const {
  kClearLine,
  kClearToLineBeginning,
  kClearToLineEnd,
} = CSI;

/**
 * moves the cursor to the x and y coordinate on the given stream
 */

function cursorTo(x, y) {
  if (NumberIsNaN(x)) throw new Error('Invalid argument x is NaN');
  if (NumberIsNaN(y)) throw new Error('Invalid argument y is NaN');
  if (typeof x !== 'number') throw new Error('Invalid argument x is not a number');

  const data = typeof y !== 'number' ? CSI`${x + 1}G` : CSI`${y + 1};${x + 1}H`;
  return data;
}

/**
 * moves the cursor relative to its current location
 */

function moveCursor(dx, dy) {
  let data = '';

  if (dx < 0) {
    data += CSI`${-dx}D`;
  } else if (dx > 0) {
    data += CSI`${dx}C`;
  }

  if (dy < 0) {
    data += CSI`${-dy}A`;
  } else if (dy > 0) {
    data += CSI`${dy}B`;
  }

  return data;
}

/**
 * clears the current line the cursor is on:
 *   -1 for left of the cursor
 *   +1 for right of the cursor
 *    0 for the entire line
 */

function clearLine(dir) {
  const type =
    dir < 0 ? kClearToLineBeginning : dir > 0 ? kClearToLineEnd : kClearLine;
  return type;
}

export {
  clearLine,
  cursorTo,
  moveCursor,
};
