declare module 'msgpackr' {
  export function pack(value: unknown): Uint8Array;
  export function unpack<T>(buffer: Uint8Array): T;
}
