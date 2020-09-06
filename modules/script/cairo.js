// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

// Merge stuff defined in the shared imports._cairo and then in native code
Object.assign(this, imports._cairo, imports.cairoNative);

