[Node Binding Specification](README.md) · [Bindings Policy](../README.md)

# Node Codec Extension Specification

This document defines the public contract for Node/TypeScript codec extension
packages. The root `@ulalax/zlink` package does not expose these entrypoints,
so applications opt in to codec dependencies explicitly.

## Packages

- `@ulalax/zlink-codec-protobuf`
- `@ulalax/zlink-codec-json`
- `@ulalax/zlink-codec-messagepack`

JSON codec baseline: built-in `JSON.parse` / `JSON.stringify`. Typed
validation may be layered on top through a schema/parser object.
MessagePack codec baseline: `@msgpack/msgpack`.

These are separate public packages layered on top of the core package. They
must not be merged into the root package entrypoint.

## Protobuf

```typescript
declare module "@ulalax/zlink-codec-protobuf" {
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
    ): import("@ulalax/zlink").Message;

    export function decode<T>(
        message: import("@ulalax/zlink").Message,
        type: ProtobufType<T>,
    ): T;
}
```

## JSON

```typescript
declare module "@ulalax/zlink-codec-json" {
    export function encode<T>(value: T): import("@ulalax/zlink").Message;
    export function decode<T>(
        message: import("@ulalax/zlink").Message,
    ): T;
}
```

## MessagePack

```typescript
declare module "@ulalax/zlink-codec-messagepack" {
    export function encode<T>(value: T): import("@ulalax/zlink").Message;
    export function decode<T>(
        message: import("@ulalax/zlink").Message,
    ): T;
}
```
