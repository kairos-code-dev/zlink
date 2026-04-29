// SPDX-License-Identifier: MPL-2.0

using MessagePack;
using Zlink;

namespace Zlink.Codecs.MessagePack;

public static class ZlinkMessagePackCodec
{
    private static MessagePackSerializerOptions _serializerOptions = MessagePackSerializerOptions.Standard;

    public static MessagePackSerializerOptions SerializerOptions => _serializerOptions;

    public static void Configure(MessagePackSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        _serializerOptions = options;
    }
}

public static class MessagePackMessageExtensions
{
    public static T FromMsgPack<T>(this Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        return MessagePackSerializer.Deserialize<T>(message.AsReadOnlyMemory(), ZlinkMessagePackCodec.SerializerOptions);
    }

    public static Message ToMsgPack<T>(this T value)
    {
        byte[] payload = MessagePackSerializer.Serialize(value, ZlinkMessagePackCodec.SerializerOptions);
        return Message.FromOwnedBytes(payload);
    }
}
