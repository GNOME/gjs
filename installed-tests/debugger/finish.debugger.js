function foo() {
    print('Print me');
    debugger;
    print('Print me also');
}

function bar() {
    print('Print me');
    debugger;
    print('Print me also');
    return 5;
}

foo();
bar();
print('Print me at the end');
