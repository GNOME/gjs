const gi = import.meta.require("gi");

export default class GIRepository {
    /**
     * 
     * @param {string} ns 
     * @param {string} version 
     */
    static require(ns, version) {
        if (version) {
            gi.versions[ns] = version;
        }

        return gi[ns];
    }
}
