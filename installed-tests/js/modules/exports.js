// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

import Gio from 'gi://Gio';

export default 5;

export const NamedExport = 'Hello, World';

const thisFile = Gio.File.new_for_uri(import.meta.url);
const dataFile = thisFile.get_parent().resolve_relative_path('data.txt');
export const [, data] = dataFile.load_contents(null);
