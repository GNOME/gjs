declare interface Window {
   debuggee: any;
}

declare var window: Window & typeof globalThis;