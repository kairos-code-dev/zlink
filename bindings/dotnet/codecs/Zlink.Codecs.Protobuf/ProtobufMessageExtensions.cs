// SPDX-License-Identifier: MPL-2.0

using Google.Protobuf;

namespace Systems.Zlink.Codecs.Protobuf;

public static class ProtobufMessageExtensions
{
    public static T FromProto<T>(this Message message)
        where T : IMessage<T>, new()
    {
        ArgumentNullException.ThrowIfNull(message);

        T value = new();
        value.MergeFrom(message.AsReadOnlySpan());
        return value;
    }

    public static Message ToProto<T>(this T value)
        where T : IMessage<T>
    {
        ArgumentNullException.ThrowIfNull(value);
        return Message.From(value.ToByteArray());
    }
}
