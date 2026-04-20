// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Text.Json;
using Zlink;

namespace Zlink.Codecs.Json;

public static class JsonMessageExtensions
{
    public static T ParseJson<T>(this Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan())!;
    }

    public static Message ToMessage<T>(this T value)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using var writer = new Utf8JsonWriter(buffer);
        JsonSerializer.Serialize(writer, value);
        writer.Flush();
        return new Message(buffer.WrittenSpan);
    }

    public static Message ToJsonMessage<T>(this T value)
    {
        return value.ToMessage();
    }
}
