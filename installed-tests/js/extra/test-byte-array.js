const ByteArray = imports.byteArray;

function foo() {
    return ByteArray.fromArray([104, 101, 108, 108, 111, 32, 116, 104, 101, 114, 101]).toString();
}

for (var i = 0; i < 400; ++i) {
    print(foo());
}

