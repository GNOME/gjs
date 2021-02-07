// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

const gi = import.meta.importSync('gi');

const Gi = {
    require(namespace, version = undefined) {
        if (namespace === 'versions')
            throw new Error('Cannot import namespace "versions", use the version parameter of Gi.require to specify versions.');

        if (version !== undefined) {
            const alreadyLoadedVersion = gi.versions[namespace];
            if (alreadyLoadedVersion !== undefined && version !== alreadyLoadedVersion) {
                throw new Error(`Version ${alreadyLoadedVersion} of GI module ${
                    namespace} already loaded, cannot load version ${version}`);
            }
            gi.versions[namespace] = version;
        }

        return gi[namespace];
    },
};
Object.freeze(Gi);

export default Gi;
