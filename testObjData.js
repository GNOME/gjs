const GObject = imports.gi.GObject;
const System = imports.system;
const Mainloop = imports.mainloop;

function objectStringify(obj) {
    return `(${System.addressOf(obj)}): ${JSON.stringify(obj)}`
}

const MyObject = GObject.registerClass({
    Properties: {
        'int-prop': GObject.ParamSpec.int('int-prop', 'int-prop', 'int-prop',
                                          GObject.ParamFlags.CONSTRUCT |
                                          GObject.ParamFlags.READWRITE, 0, 100, 5),
        'jsobj-prop': GObject.ParamSpec.jsobject('jsobj-prop', 'jsobj-prop', 'jsobj-prop',
                                                 GObject.ParamFlags.CONSTRUCT |
                                                 GObject.ParamFlags.READWRITE, ''),
    },
    Signals: {
        'send-object': { param_types: [ GObject.TYPE_JSOBJECT ] },
        'send-many-objects': { param_types: [ GObject.TYPE_JSOBJECT,
                                              GObject.TYPE_JSOBJECT,
                                              GObject.TYPE_JSOBJECT ] },
        'get-object': {
            flags: GObject.SignalFlags.RUN_LAST,
            accumulator: GObject.AccumulatorType.FIRST_WINS,
            return_type: GObject.TYPE_JSOBJECT,
            param_types: [ GObject.TYPE_JSOBJECT ],
        },
    },
}, class MyObject extends GObject.Object {
    _init() {
        super._init();

        this.connect('send-object', (obj, data) => {
            print('send-object emitted with data\n  ',
                  objectStringify(data));
        });

        this.connect('send-many-objects', (obj, o1, o2, o3) => {
            // System.gc()
            print('send-many-objects emitted with data\n  ',
                  objectStringify(o1), '\n  ',
                  objectStringify(o2), '\n  ',
                  objectStringify(o3));
            print("Sending back...")
            this.emit('send-object', o1);
        });

        this.connect('get-object', (obj, input) => {
            if (input) {
                if (input.callMe && typeof input.callMe === "function")
                    input.callMe();

                return input;
            }

            class SubObject {
                constructor() { print("Creating ",this.constructor.name); this.pi = Math.PI; }
                method() { print('In method'); }
                getGobject() { return GObject.Object };
                get data() { return { date: new Date, foo: 'bar' } }
            }

            return new SubObject();
        });
    }
    emitObject(obj) {
        if (obj === undefined)
            obj = { foo: [1, 2, 3], sub: {a: {}, 'b': this}, desc: 'test'};
        this.emit('send-object', obj);
    }
});

new MyObject({});
new MyObject({jsobj_prop: undefined});

let constructObj = new MyObject({ int_prop: 10, jsobj_prop: { date: new Date(),
                                                              int: 10,
                                                              string: 'foo' } });
print(constructObj.int_prop, JSON.stringify(constructObj.jsobj_prop));

try {
    new MyObject({ jsobj_prop: 1 });
} catch (e) {
    print(`ERROR: ${e}`);
}
try {
    new MyObject({ jsobj_prop: 'string' });
} catch (e) {
    print(`ERROR: ${e}`);
}

let obj = new MyObject();
obj.emitObject();

obj.emit('send-many-objects', [9, 8, 7], new Number(3.12), new RegExp('\\w+'));

print('get-object', obj.emit('get-object', null).constructor.name);
print('get-object', objectStringify(obj.emit('get-object', null).data));
print('get-object', new (obj.emit('get-object', null).getGobject()));
print('get-object', objectStringify(obj.emit('get-object', { array: [1, 5, 9], bar: 'foo' })));
print('get-object', objectStringify(obj.emit('get-object', { obj: GObject.Object, callMe: function() {
    print("Called by object", new Error().stack);
} })));

// obj.connect('get-object', (instance, obj) => { return true; });
// obj.emit('get-object', {});

// Mainloop.timeout_add(15000, () => { print("Leaving..."); Mainloop.quit();});
// Mainloop.run();
