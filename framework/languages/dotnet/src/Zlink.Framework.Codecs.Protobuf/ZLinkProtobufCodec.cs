using Google.Protobuf;
using System.Collections.Concurrent;
using System.Linq.Expressions;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Codecs;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Codecs.Protobuf;

public sealed class ZLinkProtobufCodec : IZLinkCodecExtension, IZlinkStreamPayloadCodec
{
    private ZLinkProtobufCodec()
    {
    }

    public static ZLinkProtobufCodec Default { get; } = new();

    public void Register(IZLinkCodecRegistrar codecs)
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

    private sealed class ProtobufSerializer :
        IZLinkMessageSerializer,
        IZLinkMessagePartSerializer,
        IZLinkMessageSpanDeserializer
    {
        private static readonly ConcurrentDictionary<Type, Func<IMessage>> Factories = new();

        public static ProtobufSerializer Instance { get; } = new();

        public ZLinkEncodedPayload Serialize(object value, Type type)
        {
            if (value is not IMessage protobuf || !typeof(IMessage).IsAssignableFrom(type))
                throw new InvalidOperationException($"Protobuf codec cannot serialize payload type '{type}'.");

            return ZLinkEncodedPayload.FromOwned(protobuf.ToByteArray());
        }

        public Message SerializePart(object value, Type type)
        {
            if (value is not IMessage protobuf || !typeof(IMessage).IsAssignableFrom(type))
                throw new InvalidOperationException($"Protobuf codec cannot serialize payload type '{type}'.");

            // Hot path: write protobuf directly into a Message buffer so the
            // framework does not allocate an intermediate byte[] per payload.
            var message = Message.Allocate(protobuf.CalculateSize());
            try
            {
                protobuf.WriteTo(message.AsSpan());
                return message;
            }
            catch
            {
                message.Dispose();
                throw;
            }
        }

        public object? Deserialize(ZLinkEncodedPayload payload, Type type)
        {
            return Deserialize(payload.Bytes.Span, type);
        }

        public object? Deserialize(ReadOnlySpan<byte> payload, Type type)
        {
            if (!typeof(IMessage).IsAssignableFrom(type))
                throw new InvalidOperationException($"Protobuf codec cannot deserialize payload type '{type}'.");

            // Hot path: compiled factories avoid Activator reflection in every
            // response decode while still honoring protobuf's parameterless ctor
            // contract.
            var protobuf = Factories.GetOrAdd(type, CreateFactory)();
            protobuf.MergeFrom(payload);
            return protobuf;
        }

        private static Func<IMessage> CreateFactory(Type type)
        {
            var constructor = type.GetConstructor(Type.EmptyTypes)
                              ?? throw new InvalidOperationException(
                                  $"{type.FullName} must have a public parameterless constructor.");
            var create = Expression.New(constructor);
            var convert = Expression.Convert(create, typeof(IMessage));
            return Expression.Lambda<Func<IMessage>>(convert).Compile();
        }
    }
}
