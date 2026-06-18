[C++ Binding Specification](README.md) · [Bindings Policy](../README.md)

# C++ Codec Extension Specification

This document defines the public contract for C++ codec extension libraries.
The core C++ binding does not expose these entrypoints from the main
`<zlink/...>` transport headers, so applications opt in to codec dependencies
explicitly.

## Libraries And Headers

- `zlink-codec-protobuf`
- `zlink-codec-json`
- `zlink-codec-messagepack`

- `<zlink/codec/protobuf.hpp>`
- `<zlink/codec/proto.hpp>` (protobuf compatibility header)
- `<zlink/codec/json.hpp>`
- `<zlink/codec/messagepack.hpp>`

JSON codec baseline: `nlohmann/json`.
MessagePack codec baseline: `msgpack-c`.

These headers are layered on top of the core C++ binding. They must not force
codec dependencies on users who only include the core binding headers.

These codec headers define only object <-> `message_t` encode/decode helpers.
Packet-name resolution, high-level serializer lookup, and typed
request/reply policy belong to framework-layer documents, not this codec
extension specification.

## Protobuf

```cpp
namespace zlink::codec::proto {

template<class T>
T decode(const message_t& message);

template<class T>
message_t encode(const T& value);

template<class T>
T parse(const message_t& message);

template<class T>
message_t to_message(const T& value);

} // namespace zlink::codec::proto

namespace zlink::codec {
namespace protobuf = proto; // compatibility namespace alias
}
```

## JSON

```cpp
namespace zlink::codec::json {

template<class T>
T decode(const message_t& message);

template<class T>
message_t encode(const T& value);

template<class T>
T parse(const message_t& message);

template<class T>
message_t to_message(const T& value);

} // namespace zlink::codec::json
```

## MessagePack

```cpp
namespace zlink::codec::messagepack {

template<class T>
T decode(const message_t& message);

template<class T>
message_t encode(const T& value);

template<class T>
T parse(const message_t& message);

template<class T>
message_t to_message(const T& value);

} // namespace zlink::codec::messagepack
```
