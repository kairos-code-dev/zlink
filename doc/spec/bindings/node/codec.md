[Node Binding Specification](./README.md) · [Bindings Policy](../README.md)

# Node Codec Extension Specification

This document defines the public contract for Node/TypeScript codec extension
packages. The root `@zlink-systems/zlink` package does not expose these
entrypoints, so applications opt in to codec dependencies explicitly.

## Packages

- `@zlink-systems/zlink-codec-protobuf`
- `@zlink-systems/zlink-codec-json`
- `@zlink-systems/zlink-codec-messagepack`

JSON codec baseline: built-in `JSON.parse` / `JSON.stringify`. Typed
validation may be layered on top through a schema/parser object.
MessagePack codec baseline: `@msgpack/msgpack`.

These are separate public packages layered on top of the core package. They
must not be merged into the root package entrypoint.

## Protobuf

```typescript
declare module "@zlink-systems/zlink-codec-protobuf" {
    export interface ProtobufType<T> {
        encode(
            message: T,
            writer?: import("protobufjs").Writer,
        ): import("protobufjs").Writer;
        decode(reader: import("protobufjs").Reader | Uint8Array): T;
    }

    export function encode<T>(
        value: T,
        type: ProtobufType<T>,
    ): import("@zlink-systems/zlink").Message;

    export function decode<T>(
        message: import("@zlink-systems/zlink").Message,
        type: ProtobufType<T>,
    ): T;
}
```

## JSON

```typescript
declare module "@zlink-systems/zlink-codec-json" {
    export function encode<T>(value: T): import("@zlink-systems/zlink").Message;
    export function decode<T>(
        message: import("@zlink-systems/zlink").Message,
    ): T;
}
```

## MessagePack

```typescript
declare module "@zlink-systems/zlink-codec-messagepack" {
    export function encode<T>(value: T): import("@zlink-systems/zlink").Message;
    export function decode<T>(
        message: import("@zlink-systems/zlink").Message,
    ): T;
}
```
