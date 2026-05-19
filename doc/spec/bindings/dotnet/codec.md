[.NET Binding Specification](./README.md) · [Bindings Policy](../README.md)

# .NET Codec Extension Specification

This document defines the public contract for .NET codec extension libraries.
The core `Systems.Zlink` assembly does not expose these entrypoints, so
applications opt in to codec dependencies explicitly.

## Packages And Namespaces

| Package | Namespace | Baseline |
|---------|-----------|----------|
| `Systems.Zlink.Codecs.Protobuf` | `Systems.Zlink.Codecs.Protobuf` | Google.Protobuf |
| `Systems.Zlink.Codecs.Json` | `Systems.Zlink.Codecs.Json` | System.Text.Json |
| `Systems.Zlink.Codecs.MessagePack` | `Systems.Zlink.Codecs.MessagePack` | MessagePack for C# |

These extensions are separate public modules layered on top of the core
binding. They must not be merged into the `Systems.Zlink` core assembly.

## Protobuf

```csharp
namespace Systems.Zlink.Codecs.Protobuf;

public static class ProtobufMessageExtensions
{
    T FromProto<T>(this Message message)
        where T : Google.Protobuf.IMessage<T>, new();

    Message ToProto<T>(this T value)
        where T : Google.Protobuf.IMessage<T>;
}
```

## JSON

```csharp
namespace Systems.Zlink.Codecs.Json;

public static class JsonMessageExtensions
{
    T FromJson<T>(
        this Message message,
        System.Text.Json.JsonSerializerOptions? options = null);

    Message ToJson<T>(
        this T value,
        System.Text.Json.JsonSerializerOptions? options = null);
}
```

## MessagePack

```csharp
namespace Systems.Zlink.Codecs.MessagePack;

public static class MessagePackMessageExtensions
{
    T FromMsgPack<T>(
        this Message message,
        MessagePack.MessagePackSerializerOptions? options = null);

    Message ToMsgPack<T>(
        this T value,
        MessagePack.MessagePackSerializerOptions? options = null);
}
```
