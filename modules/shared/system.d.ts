declare module "system" {
  export function addressOf(obj: object): string;

  export function addressOfGObject(gobj: GObject.Object): string;

  export function refcount(obj: object): number;

  export function breakpoint();

  export function dumpHeap(filename: string): void;

  export function gc(): void;

  export function exit(code: number): void;

  export function clearDateCaches(): void;
}
