[Go Binding Specification](README.md) · [Bindings Policy](../README.md)

# Go Codec Extension Specification

This document defines the public contract for Go codec extension modules. The
root `zlink.systems/zlink` package does not expose these entrypoints, so
applications opt in to codec dependencies explicitly.

## Packages

- `zlink.systems/zlink/codec/proto`
- `zlink.systems/zlink/codec/json`
- `zlink.systems/zlink/codec/messagepack`

These are separate public packages layered on top of the core
`zlink.systems/zlink` package. They must not be folded into the root package as
required dependencies.

These codec packages define only object <-> `Message` encode/decode helpers.
Packet-name resolution, high-level serializer lookup, and typed
request/reply policy belong to framework-layer documents, not this codec
extension specification.

JSON codec baseline: `encoding/json`.
MessagePack codec baseline: `vmihailenco/msgpack/v5`.

## Protobuf

```go
package proto

func Decode[T google.golang.org/protobuf/proto.Message](
    message *zlink.Message,
) (T, error)

func Encode[T google.golang.org/protobuf/proto.Message](
    value T,
) (*zlink.Message, error)
```

## JSON

```go
package json

func Decode[T any](message *zlink.Message) (T, error)
func Encode[T any](value T) (*zlink.Message, error)
```

## MessagePack

```go
package messagepack

func Decode[T any](message *zlink.Message) (T, error)
func Encode[T any](value T) (*zlink.Message, error)
```
