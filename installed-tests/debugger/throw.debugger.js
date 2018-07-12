function a() {
    debugger;
    return 5;
}

try {
    a();
} catch (e) {
    print(`Exception: ${e}`);
}
