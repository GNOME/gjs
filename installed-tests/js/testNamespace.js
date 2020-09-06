// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

const Regress = imports.gi.Regress;

describe('GI repository namespace', function () {
    it('supplies a name', function () {
        expect(Regress.__name__).toEqual('Regress');
    });
});
