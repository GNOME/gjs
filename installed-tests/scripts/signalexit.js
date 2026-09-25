// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// this JS script is used to test correct exiting from signal callbacks
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import {exit} from 'system';

const Button = GObject.registerClass({
    Signals: {
        'clicked': {},
    },
}, class Button extends GObject.Object {
    go() {
        this.emit('clicked');
    }
});

const button = new Button();
button.connect('clicked', () => exit(15));
let n = 1;
GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 2, () => {
    print(`click ${n++}`);
    button.go();
    return GLib.SOURCE_CONTINUE;
});
const loop = new GLib.MainLoop(null, false);
loop.run();
