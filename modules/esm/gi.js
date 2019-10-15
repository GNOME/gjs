const gi = window.require("gi");

/**
 * 
 * @param {string} ns 
 * @param {string} version 
 */
export default class GIRepository {
    static require(ns, version) {
        if (version) {
            gi.versions[ns] = version;
        }

        return gi[ns];
    }
}
