const gi = import.meta.require("gi");

// Introduce an additional map on top of the existing native gi module.
const modules = new Map();

export default class GIRepository {
    /**
     * 
     * @param {string} ns 
     * @param {string} [version] 
     */
    static require(ns, version) {
        if(!modules.has(ns)) {
            if (version) {
                gi.versions[ns] = version;
            }

            modules.set(ns, gi[ns]);
        }

        return modules.get(ns);        
    }
}

// If you don't force these to load first bad things happen.

// I believe it may be because Gtk/Gio/GLib depends on GObject and
// if it is not correctly loaded _first_ view interdependencies emerge.
// TODO Figure out exact reason though!

GIRepository.require('GjsPrivate');
GIRepository.require('GObject');
