declare var imports: undefined | { [key: string]: any };

declare interface StringConstructor {
    $gtype: any;
}

declare interface NumberConstructor {
    $gtype: any;
}

declare interface BooleanConstructor {
    $gtype: any;
}

declare interface Error {
    matches(err: any): boolean;
}