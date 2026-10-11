// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

// this JS script should fail to import a second version of the same namespace
import 'gi://Gio?version=2.0';
import 'gi://Gio?version=75.94';
