// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// Bootstrap file which supports ESM imports.

// Bootstrap the Encoding API
import '_encoding/encoding';
// Bootstrap the Console API
import 'console';
// Bootstrap the Timers API
import '_timers';
// Install the Repl constructor for Console.interact()
import 'repl';
// Install the pretty printing global function
import '_prettyPrint';
// Setup legacy global values
import '_legacyGlobal';
