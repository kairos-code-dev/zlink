[Java Binding Specification](./README.md) · [Bindings Policy](../README.md)

# Java Codec Extension Specification

This document defines the public contract for Java codec extension artifacts.
The core Java binding does not expose these entrypoints from
`systems.zlink`, so applications opt in to codec dependencies
explicitly.

## Artifacts And Packages

- Maven `zlink-codec-protobuf`
- Maven `zlink-codec-json`
- Maven `zlink-codec-messagepack`

- `systems.zlink.codec.protobuf`
- `systems.zlink.codec.json`
- `systems.zlink.codec.messagepack`

JSON codec baseline: `Jackson`.
MessagePack codec baseline: `jackson-dataformat-msgpack`.

## Protobuf

```java
package systems.zlink.codec.protobuf;

public final class ProtobufCodec {
    public static <T extends com.google.protobuf.MessageLite> T parseProto(
        systems.zlink.Message message,
        com.google.protobuf.Parser<T> parser);

    public static systems.zlink.Message toMessage(
        com.google.protobuf.MessageLite value);
}
```

## JSON

```java
package systems.zlink.codec.json;

public final class JsonCodec {
    public static <T> T parseJson(
        systems.zlink.Message message,
        Class<T> type);

    public static systems.zlink.Message toMessage(Object value);
}
```

## MessagePack

```java
package systems.zlink.codec.messagepack;

public final class MessagePackCodec {
    public static <T> T parseMessagePack(
        systems.zlink.Message message,
        Class<T> type);

    public static systems.zlink.Message toMessage(Object value);
}
```
