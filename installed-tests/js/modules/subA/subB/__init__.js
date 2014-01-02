function testImporterFunction() {
    return "__init__ function tested";
}

function ImporterClass() {
    this._init();
}

ImporterClass.prototype = {
    _init : function() {
        this._a = "__init__ class tested";
    },

    testMethod : function() {
        return this._a;
    }
}
