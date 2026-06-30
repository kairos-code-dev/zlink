using Google.Protobuf;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Codecs;

namespace Zlink.Framework.Codecs.Protobuf;

public sealed class ZLinkProtobufCodec : IZLinkCodecExtension, IZlinkStreamPayloadCodec
{
    private ZLinkProtobufCodec()
    {
    }

    public static ZLinkProtobufCodec Default { get; } = new();

    public void Register(IZLinkCodecRegistryBuilder codecs)
    {
        ArgumentNullException.ThrowIfNull(codecs);
        codecs.AddSerializer(
            "application/x-protobuf",
            ProtobufSerializer.Instance,
            type => typeof(IMessage).IsAssignableFrom(type));
        codecs.AddStreamCodec("application/x-protobuf", ZlinkStreamCodec.Protobuf);
    }

    public ZlinkStreamEncodedPayload Encode<TPayload>(TPayload payload)
    {
        if (payload is not IMessage protobuf)
            throw new InvalidOperationException($"Protobuf codec cannot encode payload type '{typeof(TPayload)}'.");

        return new ZlinkStreamEncodedPayload(
            ZlinkStreamCodec.Protobuf,
            protobuf.ToByteArray(),
            typeof(TPayload));
    }

    public TPayload Decode<TPayload>(ZlinkStreamEncodedPayload payload)
    {
        if (payload.Codec != ZlinkStreamCodec.Protobuf)
            throw new InvalidOperationException($"Stream payload codec is {payload.Codec}, not Protobuf.");

        if (!typeof(IMessage).IsAssignableFrom(typeof(TPayload)))
            throw new InvalidOperationException($"Protobuf codec cannot decode payload type '{typeof(TPayload)}'.");

        var protobuf = (IMessage?)Activator.CreateInstance(typeof(TPayload))
                       ?? throw new InvalidOperationException(
                           $"{typeof(TPayload).FullName} must have a public parameterless constructor.");
        protobuf.MergeFrom(payload.Payload.Span);
        return (TPayload)protobuf;
    }

    private sealed class ProtobufSerializer : IZLinkMessageSerializer
    {
        public static ProtobufSerializer Instance { get; } = new();

        public ZLinkEncodedPayload Serialize(object value, Type type)
        {
            if (value is not IMessage protobuf || !typeof(IMessage).IsAssignableFrom(type))
                throw new InvalidOperationException($"Protobuf codec cannot serialize payload type '{type}'.");

            return ZLinkEncodedPayload.From(protobuf.ToByteArray());
        }

        public object? Deserialize(ZLinkEncodedPayload payload, Type type)
        {
            if (!typeof(IMessage).IsAssignableFrom(type))
                throw new InvalidOperationException($"Protobuf codec cannot deserialize payload type '{type}'.");

            var protobuf = (IMessage?)Activator.CreateInstance(type)
                           ?? throw new InvalidOperationException(
                               $"{type.FullName} must have a public parameterless constructor.");
            protobuf.MergeFrom(payload.Bytes.Span);
            return protobuf;
        }
    }
}