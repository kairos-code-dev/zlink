[.NET Binding Specification](README.md) · [Bindings Policy](../README.md)

# .NET Codec Extension Specification

This document defines the public contract for .NET codec extension libraries.
The core `Zlink` assembly does not expose these entrypoints, so applications
opt in to codec dependencies explicitly.

## Packages And Namespaces

| Package | Namespace | Baseline |
|---------|-----------|----------|
| `Zlink.Codecs.Protobuf` | `Zlink.Codecs.Protobuf` | Google.Protobuf |
| `Zlink.Codecs.Json` | `Zlink.Codecs.Json` | System.Text.Json |
| `Zlink.Codecs.MessagePack` | `Zlink.Codecs.MessagePack` | MessagePack for C# |

These extensions are separate public modules layered on top of the core
binding. They must not be merged into the `Zlink` core assembly.

## Protobuf

```csharp
namespace Zlink.Codecs.Protobuf;

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
namespace Zlink.Codecs.Json;

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
namespace Zlink.Codecs.MessagePack;

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
