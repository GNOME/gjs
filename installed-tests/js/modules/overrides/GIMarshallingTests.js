// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

function _init() {
    const GIMarshallingTests = this;

    GIMarshallingTests.OVERRIDES_CONSTANT = 7;

    GIMarshallingTests.OverridesStruct.prototype._real_method =
        GIMarshallingTests.OverridesStruct.prototype.method;
    GIMarshallingTests.OverridesStruct.prototype.method = function () {
        return this._real_method() / 7;
    };

    GIMarshallingTests.OverridesObject.prototype._realInit =
        GIMarshallingTests.OverridesObject.prototype._init;
    GIMarshallingTests.OverridesObject.prototype._init = function (num, ...args) {
        this._realInit(...args);
        this.num = num;
    };

    GIMarshallingTests.OverridesObject.prototype._realMethod =
        GIMarshallingTests.OverridesObject.prototype.method;
    GIMarshallingTests.OverridesObject.prototype.method = function () {
        return this._realMethod() / 7;
    };
}
