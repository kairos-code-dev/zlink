// SPDX-License-Identifier: MPL-2.0

using System;
using MessagePack;
using Systems.Zlink;

namespace Systems.Zlink.Codecs.MessagePack;

public static class MessagePackMessageExtensions
{
    public static T FromMsgPack<T>(this Message message,
        MessagePackSerializerOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        return MessagePackSerializer.Deserialize<T>(message.AsReadOnlyMemory(),
            options);
    }

    public static Message ToMsgPack<T>(this T value,
        MessagePackSerializerOptions? options = null)
    {
        byte[] payload = MessagePackSerializer.Serialize(value, options);
        return Message.FromBytes(payload);
    }
}
