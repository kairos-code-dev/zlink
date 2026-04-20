// SPDX-License-Identifier: MPL-2.0

using Google.Protobuf;
using Zlink;

namespace Zlink.Codecs.Protobuf;

public static class ProtoCodec
{
    public static T Decode<T>(this Message message, MessageParser<T> parser)
        where T : IMessage<T>
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(parser);
        return parser.ParseFrom(message.AsReadOnlySpan().ToArray());
    }

    public static Message Encode<T>(this T value)
        where T : IMessage<T>
    {
        ArgumentNullException.ThrowIfNull(value);
        return Message.FromBytes(value.ToByteArray());
    }
}
