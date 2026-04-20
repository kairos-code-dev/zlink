// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using MessagePack;
using Zlink;

namespace Zlink.Codecs.MessagePack;

public static class MessagePackMessageExtensions
{
    public static T ParseMessagePack<T>(this Message message)
    {
        ArgumentNullException.ThrowIfNull(message);

        ReadOnlySpan<byte> payload = message.AsReadOnlySpan();
        ReadOnlyMemory<byte> buffer = payload.IsEmpty
            ? ReadOnlyMemory<byte>.Empty
            : payload.ToArray();

        return MessagePackSerializer.Deserialize<T>(buffer);
    }

    public static Message ToMessage<T>(this T value)
    {
        var buffer = new ArrayBufferWriter<byte>();
        MessagePackSerializer.Serialize(buffer, value);
        return new Message(buffer.WrittenSpan);
    }

    public static Message ToMessagePackMessage<T>(this T value)
    {
        return value.ToMessage();
    }
}
