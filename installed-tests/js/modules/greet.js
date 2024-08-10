// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Philip Chimento <philip.chimento@gmail.com>

import GLib from 'gi://GLib';

const uri = GLib.Uri.parse(import.meta.url, GLib.UriFlags.NONE);
const {greeting, name} = GLib.Uri.parse_params(uri.get_query(), -1, '&', GLib.UriParamsFlags.NONE);

export default `${greeting}, ${name}`;
