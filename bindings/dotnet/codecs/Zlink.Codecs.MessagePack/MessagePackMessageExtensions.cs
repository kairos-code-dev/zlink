// SPDX-License-Identifier: MPL-2.0

using System;
using MessagePack;
using Zlink;

namespace Zlink.Codecs.MessagePack;

public static class MessagePackMessageExtensions
{
    public static T ParseMessagePack<T>(this Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return MessagePackSerializer.Deserialize<T>(message.AsReadOnlyMemory());
    }

    public static Message ToMessage<T>(this T value)
    {
        byte[] payload = MessagePackSerializer.Serialize(value);
        return Message.FromOwnedBytes(payload);
    }

    public static Message ToMessagePackMessage<T>(this T value)
    {
        return value.ToMessage();
    }
}
