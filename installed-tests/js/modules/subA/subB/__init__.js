/* exported ImporterClass, testImporterFunction */

function testImporterFunction() {
    return '__init__ function tested';
}

function ImporterClass() {
    this._init();
}

ImporterClass.prototype = {
    _init() {
        this._a = '__init__ class tested';
    },

    testMethod() {
        return this._a;
    },
};
