const Soup = imports.gi.Soup;
const Lang = imports.lang;
const GLib = imports.gi.GLib;

let loop = GLib.MainLoop.new(null, false);

function do_show(p) {
    /* Parse the data in JSON format */
    var data = JSON.parse(p);
    print(Object.getOwnPropertyNames(data));

    data.RelatedTopics.forEach(function(m) {  

        if (typeof m.Result !== 'undefined')
            print('Result: ' + m.Result); 
    });
}

function do_search(w) {
    let _httpSession = new Soup.Session();
    let params = {
        q: w,
        format: 'json',
        no_html: '1',
        pretty: '1',
        skip_disambig: '1'
    };

    let message = Soup.form_request_new_from_hash('GET', 'http://api.duckduckgo.com', params);
    _httpSession.queue_message(message, Lang.bind(this, function(_httpSession, message) {
        try {
            if (!message.response_body.data) {
                return;
            }
            do_show(message.response_body.data);
        } catch (e) {
            print(e);
            print('Error while acessing data');
        }
        loop.quit();
        return;
    }));
    loop.run();
}

do_search('javascript');


