// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.

import Regress from 'gi://Regress';

describe('GI repository namespace', function () {
    it('supplies a name', function () {
        expect(Regress.__name__).toEqual('Regress');
    });
});
