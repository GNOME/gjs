(function(exports) {
    'use strict';
// Save standard built-ins before scripts can modify them.
const ArrayPrototypeJoin = Array.prototype.join;
const MapPrototypeGet = Map.prototype.get;
const MapPrototypeHas = Map.prototype.has;
const MapPrototypeSet = Map.prototype.set;
const ObjectDefineProperty = Object.defineProperty;
const ReflectApply = Reflect.apply;
const StringPrototypeIndexOf = String.prototype.indexOf;
const StringPrototypeLastIndexOf = String.prototype.lastIndexOf;
const StringPrototypeStartsWith = String.prototype.startsWith;
const StringPrototypeSubstring = String.prototype.substring;

const ReflectLoader = new class {
    constructor() {
        this.registry = new Map();
        this.modulePaths = new Map();
        this.loadPath = imports.gi.GLib.get_current_dir();
    }
    resolve(name, module) {
        log("resolve(" + name + ", " + module);
        if (imports.gi.GLib.path_is_absolute(name))
            return name;

        let loadPath = this.loadPath;
        if (module) {
            // Treat |name| as a relative path if it starts with either "./"
            // or "../".
            let isRelative = ReflectApply(StringPrototypeStartsWith, name, ["./"])
                          || ReflectApply(StringPrototypeStartsWith, name, ["../"])
                             ;

            // If |name| is a relative path and |module|'s path is available,
            // load |name| relative to the referring module.
            if (isRelative && ReflectApply(MapPrototypeHas, this.modulePaths, [module])) {
                let modulePath = ReflectApply(MapPrototypeGet, this.modulePaths, [module]);
                let sepIndex = ReflectApply(StringPrototypeLastIndexOf, modulePath, ["/"]);
                if (sepIndex >= 0)
                    loadPath = ReflectApply(StringPrototypeSubstring, modulePath, [0, sepIndex]);
            }
        }

        log("loadPath: " + loadPath);
        const file = imports.gi.Gio.file_new_for_commandline_arg_and_cwd(name, loadPath);
        return file.get_path();
    }

    normalize(path) {
        const pathsep =
        "/";

        let n = 0;
        let components = [];

        // Normalize the path by removing redundant path components.
        // NB: See above for why we don't call String.prototype.split here.
        let lastSep = 0;
        while (lastSep < path.length) {
            let i = ReflectApply(StringPrototypeIndexOf, path, [pathsep, lastSep]);
            if (i < 0)
                i = path.length;
            let part = ReflectApply(StringPrototypeSubstring, path, [lastSep, i]);
            lastSep = i + 1;

            // Remove "." when preceded by a path component.
            if (part === "." && n > 0)
                continue;

            if (part === ".." && n > 0) {
                // Replace "./.." with "..".
                if (components[n - 1] === ".") {
                    components[n - 1] = "..";
                    continue;
                }

                // When preceded by a non-empty path component, remove ".." and
                // the preceding component, unless the preceding component is also
                // "..".
                if (components[n - 1] !== "" && components[n - 1] !== "..") {
                    components.length = --n;
                    continue;
                }
            }

            ObjectDefineProperty(components, n++, {
                __proto__: null,
                value: part,
                writable: true, enumerable: true, configurable: true
            });
        }

        let normalized = ReflectApply(ArrayPrototypeJoin, components, [pathsep]);
        return normalized;
    }

    fetch(path) {
        let fileContents = String(imports.gi.GLib.file_get_contents(path + ".js")[1]);
        return fileContents;
    }
    loadAndParse(path) {
        log("Normalizing: " + path);
        let normalized = this.normalize(path);
        log("Loading: " + normalized);
        if (ReflectApply(MapPrototypeHas, this.registry, [normalized]))
            return ReflectApply(MapPrototypeGet, this.registry, [normalized]);
        let source = this.fetch(normalized);
        let module = parseModule(source, path);
        ReflectApply(MapPrototypeSet, this.registry, [normalized, module]);
        ReflectApply(MapPrototypeSet, this.modulePaths, [module, path]);
        return module;
    }

    loadAndExecute(path) {
        let module = this.loadAndParse(path);
        module.declarationInstantiation();
        return module.evaluation();
    }

    importRoot(path) {
        return this.loadAndExecute(path);
    }

    ["import"](name, referrer) {
        let path = this.resolve(name, null);
        return this.loadAndExecute(path);
    }
};

    setModuleResolveHook((module, requestName) => {
        try {
            let path = ReflectLoader.resolve(requestName, module);
            return ReflectLoader.loadAndParse(path);
        } catch (e) {
            logError(e);
        }

});

Reflect.Loader = ReflectLoader;
})(window);
