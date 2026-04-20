// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using Google.Protobuf;
using Zlink;

namespace Zlink.Codecs.Protobuf;

public static class ProtobufMessageExtensions
{
    public static T ParseProto<T>(this Message message)
        where T : IMessage<T>, new()
    {
        ArgumentNullException.ThrowIfNull(message);

        T value = new();
        value.MergeFrom(message.AsReadOnlySpan());
        return value;
    }

    public static Message ToMessage<T>(this T value)
        where T : IMessage<T>
    {
        ArgumentNullException.ThrowIfNull(value);

        var buffer = new ArrayBufferWriter<byte>(value.CalculateSize());
        value.WriteTo(buffer);
        return new Message(buffer.WrittenSpan);
    }

    public static Message ToProtoMessage<T>(this T value)
        where T : IMessage<T>
    {
        return value.ToMessage();
    }
}
