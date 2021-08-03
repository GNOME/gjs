// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

const gi = import.meta.importSync('gi');

const Gi = {
    require(namespace, version = undefined) {
        if (namespace === 'versions')
            throw new Error('Cannot import namespace "versions", use the version parameter of Gi.require to specify versions.');

        if (version !== undefined)
            gi.versions[namespace] = version;

        const module = gi[namespace];

        if (version !== undefined && version !== module.__version__) {
            throw new Error(`Version ${module.__version__} of GI module ${
                namespace} already loaded, cannot load version ${version}`);
        }

        return module;
    },
};
Object.freeze(Gi);

export default Gi;
