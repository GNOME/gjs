/// <reference path="./default.js" />

declare class Debugger {
    collectCoverageInfo: boolean;
  constructor(...globals);
  onPromiseSettled: (promise: DebugPromise) => void;
  onNewPromise: (promise: DebugPromise) => void;
  addDebuggee(debuggee: any): Debugger.Object;
  onDebuggerStatement: (frame: {
    evalWithBindings?: (arg0: any, arg1: {}) => void;
    eval?: (arg0: any) => void;
    older?: Frame;
    younger?: Frame;
    describeFull: any;
    script?: unknown;
  }) => any;
  onExceptionUnwind: (frame: Frame, value: any) => any;
  findScripts(arg0: { line: number; url: any }): Debugger.Script[];
  enabled: boolean;
}

declare namespace Debugger {
  declare class Object {
    executeInGlobalWithBindings(
      expr: string,
      debuggeeValues: { [x: string]: any }
    ): any;
  }

  declare class Script {
    setBreakpoint(offset: number, bp: BreakpointHandler);
    url: any;
    getLineOffsets(line: number): number[];
    getOffsetLocation(offset: any): { lineNumber: any; columnNumber: any };
    describeOffset: (offset: any) => string;
  }

  declare class Frame {
    offset(offset: any): any;
    describeFrame: (...args: any[]) => string;
    describePosition: () => any;
    describeFull: () => string;
    type: string;
    callee: any;
    arguments: any;
    script: any;
  }
}

declare function uneval(a: any): string;
declare function readline(): string;

declare interface Window {
  [key: string]: any;
  debuggee: any;
  debugger: Debugger;
}

