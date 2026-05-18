using System.Collections.Concurrent;
using System.Linq.Expressions;
using Google.Protobuf;
using MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using Systems.Zlink.Stream.Connector.MessagePack;
using Systems.Zlink.Stream.Connector.Protobuf;

namespace Systems.Zlink.Stream.Connector.Codecs;

internal static class ZlinkStreamAutoCodecSelector
{
    private static readonly ConcurrentDictionary<Type, bool> MessagePackTypes = new();
    private static readonly ConcurrentDictionary<Type, Func<IMessage>> ProtobufFactories = new();

    public static ZlinkStreamEncodedPayload Encode<TPayload>(TPayload payload)
    {
        ArgumentNullException.ThrowIfNull(payload);

        if (payload is IMessage protobuf)
        {
            return new ZlinkStreamEncodedPayload(
                ZlinkStreamCodec.Protobuf,
                protobuf.ToByteArray(),
                typeof(TPayload));
        }

        if (HasMessagePackObjectAttribute(typeof(TPayload)))
        {
            return payload.ToMsgPack();
        }

        return payload.ToJson();
    }

    public static TPayload Decode<TPayload>(ZlinkStreamEncodedPayload payload)
    {
        if (typeof(IMessage).IsAssignableFrom(typeof(TPayload)))
        {
            return DecodeProtobuf<TPayload>(payload);
        }

        if (HasMessagePackObjectAttribute(typeof(TPayload)))
        {
            return payload.FromMsgPack<TPayload>();
        }

        return payload.FromJson<TPayload>();
    }

    private static TPayload DecodeProtobuf<TPayload>(ZlinkStreamEncodedPayload payload)
    {
        if (payload.Codec != ZlinkStreamCodec.Protobuf)
        {
            throw new InvalidOperationException($"Stream payload codec is {payload.Codec}, not Protobuf.");
        }

        var message = ProtobufFactories.GetOrAdd(typeof(TPayload), CreateProtobufFactory)();

        message.MergeFrom(payload.Payload.Span);
        return (TPayload)message;
    }

    private static bool HasMessagePackObjectAttribute(Type type)
        => MessagePackTypes.GetOrAdd(
            type,
            static candidate => candidate.GetCustomAttributes(typeof(MessagePackObjectAttribute), inherit: true).Length > 0);

    private static Func<IMessage> CreateProtobufFactory(Type type)
    {
        if (!typeof(IMessage).IsAssignableFrom(type))
        {
            throw new InvalidOperationException($"{type.FullName} must implement IMessage.");
        }

        var constructor = type.GetConstructor(Type.EmptyTypes)
            ?? throw new InvalidOperationException(
                $"{type.FullName} must have a public parameterless constructor.");
        var instance = Expression.New(constructor);
        var convert = Expression.Convert(instance, typeof(IMessage));
        return Expression.Lambda<Func<IMessage>>(convert).Compile();
    }
}
