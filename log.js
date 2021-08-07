const {GLib} =imports.gi;

GLib.log_set_writer_func(GLib.log_writer_default);

GLib.log_structured('Gjs-Console-Test', GLib.LogLevelFlags.LEVEL_MESSAGE, { 'MESSAGE': 'TESTING'});