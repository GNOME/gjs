function a() {
    b();
}

function b() {
    c();
}

function c() {
    d();
}

function d() {
    debugger;
}

a();
