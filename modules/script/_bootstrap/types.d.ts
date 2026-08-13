/// @ts-check


declare const debuggee = "__DEBUGEE__OPAQUE__";
declare function loadNative(name: "_print"): {
    print: (...args: unknown[]) => void;
    printerr: (...args: unknown[]) => void;
};
declare function loadNative(name: "_encodingNative"): {
    encode: (str: string, charset: string) => Uint8Array;
    decode: (buf: Uint8Array, charset: string) => string;
};
declare function loadNative(name: string): unknown;

// Exposed functions

type InputStream = "__INPUT_STREAM__OPAQUE__";

declare function openInputStream(stream: number): InputStream;
declare function readLine(stream: InputStream): string;
declare function readBytes(stream: InputStream, bytes: number): string;
declare function launchFile(path: string): void;
declare function quit(exitCode: number): void;

// Debugger types

declare namespace DAP {
    interface ProtocolMessage {
        seq: number;
        type: "request" | "response" | "event";
    }

    interface Request extends ProtocolMessage {
        type: "request";
        command: string;
        arguments?: any;
    }

    interface Event extends ProtocolMessage {
        type: "event";
        event: string;
        body?: any;
    }

    interface Response extends ProtocolMessage {
        type: "response";
        request_seq: number;
        success: boolean;
        command: string;
        message?: "cancelled" | "notStopped";
        body?: any;
    }

    interface Message {
        id: number;
        format: string;
        variables?: Record<string, string>;
        showUser?: boolean;
    }

    interface ErrorResponse extends Response {
        success: false;
        body: {
            error: Message;
        };
    }

    type Thing = Request | Response | Event;
}

// Debugger API

declare class Debugger {
    // events
    onNewScript: undefined | ((script: Debugger.Script) => void);
    onEnterFrame: undefined | ((frame: Debugger.Frame) => void);

    // methods
    addDebuggee(debugee: any): void;
    findScripts(condition?: Partial<{ url: string }>): Debugger.Script[];
    getNewestFrame(): Debugger.Frame | null;
}

declare namespace Debugger {
    interface BreakpointHandler {
        hit(frame: Debugger.Frame): void;
    }

    // opaque
    class Offset {}

    class Frame {
        older: Debugger.Frame | null;
        depth: number;
        /** @deprecated use .onStack */
        live: boolean;
        onStack: boolean;
        script: Debugger.Script | null;
        offset: Debugger.Offset | null;
        environment: Debugger.Environment | null;
    }

    class Script {
        url: string;
        displayName: string;
        setBreakpoint(offset: Offset, handler: BreakpointHandler): void;
        clearAllBreakpoints(offset?: Offset): void;
        getLineOffsets(line: number): Offset[];
        getOffsetLocation(
            offset: Offset,
        ): null | {
            lineNumber: number;
            columnNumber: number;
            isEntryPoint: boolean;
        };
    }

    class Environment {
        type: "declarative" | "object" | "with";
        parent: Debugger.Environment | null;
        // throws when type is `declarative`
        object: Debugger.Object | null;
    }

    class Object {
        name: string | undefined;
        displayName: string | undefined;
        getVariable(name: string): Debugger.Value | null;
        isPromise: boolean;
        script: Debugger.Script | null;
    }

    class Value {}
}
