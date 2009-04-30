const DBus = imports.dbus;
const Mainloop = imports.mainloop;

let bus = DBus.session;

var notifyIface = {
    name: "org.freedesktop.Notifications",
    methods: [{ name: "Notify",
                outSignature: "u",
                inSignature: "susssasa{sv}i"
              }
             ]
};

function Notify() {
    this._init();
};

Notify.prototype = {
     _init: function() {
         DBus.session.proxifyObject(this, 'org.freedesktop.Notifications', '/org/freedesktop/Notifications');
     }

};
DBus.proxifyPrototype(Notify.prototype,
                      notifyIface);

let notify = new Notify();

notify.NotifyRemote("test", 0, "", "TestNotify", "Hello from Test Notify", [], {}, 0, function(result, excp) { Mainloop.quit('test'); });

Mainloop.run('test');



