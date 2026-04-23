// SPDX-License-Identifier: MPL-2.0

using MessagePack;
using Zlink;

namespace Zlink.Codecs.MessagePack;

public static class MessagePackCodec
{
    public static T Decode<T>(this Message message, MessagePackSerializerOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        return MessagePackSerializer.Deserialize<T>(message.AsReadOnlySpan(), options);
    }

    public static Message Encode<T>(this T value, MessagePackSerializerOptions? options = null)
    {
        byte[] payload = MessagePackSerializer.Serialize(value, options);
        return Message.FromOwnedBytes(payload);
    }
}
