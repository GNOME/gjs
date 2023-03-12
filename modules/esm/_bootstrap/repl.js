// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

import {Repl} from 'repl';

const repl = new Repl();

globalThis.repl = repl;

repl.start();
