const Gio = imports.gi.Gio;
const Mainloop = imports.mainloop;

function cat(filename) {
    let f = Gio.file_new_for_path(filename);
    f.load_contents_async(null, function(f, res) {
        let contents;
        try {
            contents = f.load_contents_finish(res)[1];
        } catch (e) {
            log("*** ERROR: " + e.message);
            Mainloop.quit('');
            return;
        }
        print(contents);
        Mainloop.quit('');
    }, null);

    Mainloop.run('');
}

if (ARGV.length != 1) {
    printerr("Usage: gio-cat.js filename");
} else {
    cat(ARGV[0]);
}
