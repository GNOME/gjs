// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 GNOME Foundation

// Simple WASM test module exporting an add function.
//
// To regenerate add.wasm:
//   clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export=add -O2 -o
//   add.wasm add.c

int add(int a, int b) { return a + b; }
