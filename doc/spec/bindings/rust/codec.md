[Rust Binding Specification](README.md) · [Bindings Policy](../README.md)

# Rust Codec Extension Specification

This document defines the public contract for Rust codec extension crates. The
core `zlink` crate does not expose these entrypoints, so applications opt in
to codec dependencies explicitly.

## Crates

- crate `zlink-codec-protobuf` -> `zlink_codec_protobuf`
- crate `zlink-codec-json` -> `zlink_codec_json`
- crate `zlink-codec-messagepack` -> `zlink_codec_messagepack`

These are separate public crates layered on top of the core `zlink` crate.
They must not become required dependencies of the core crate.

JSON codec baseline: `serde_json`.
MessagePack codec baseline: `rmp-serde`.

## Protobuf

```rust
// zlink_codec_protobuf
pub fn decode<T>(message: &zlink::Message) -> Result<T, Error>
where
    T: prost::Message + Default;

pub fn encode<T>(value: &T) -> Result<zlink::Message, Error>
where
    T: prost::Message;
```

## JSON

```rust
// zlink_codec_json
pub fn decode<T>(message: &zlink::Message) -> Result<T, Error>
where
    T: serde::de::DeserializeOwned;

pub fn encode<T>(value: &T) -> Result<zlink::Message, Error>
where
    T: serde::Serialize;
```

## MessagePack

```rust
// zlink_codec_messagepack
pub fn decode<T>(message: &zlink::Message) -> Result<T, Error>
where
    T: serde::de::DeserializeOwned;

pub fn encode<T>(value: &T) -> Result<zlink::Message, Error>
where
    T: serde::Serialize;
```

Each codec crate defines its own `Error` type. The helper reads from
`Message::as_bytes()` and creates new frames with `Message::from_bytes()`.
