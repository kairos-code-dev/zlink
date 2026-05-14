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

    public static ZlinkStreamEncodedBody Encode<TBody>(TBody body)
    {
        ArgumentNullException.ThrowIfNull(body);

        if (body is IMessage protobuf)
        {
            return new ZlinkStreamEncodedBody(
                ZlinkStreamCodec.Protobuf,
                protobuf.ToByteArray(),
                typeof(TBody));
        }

        if (HasMessagePackObjectAttribute(typeof(TBody)))
        {
            return body.ToMsgPack();
        }

        return body.ToJson();
    }

    public static TBody Decode<TBody>(ZlinkStreamEncodedBody body)
    {
        if (typeof(IMessage).IsAssignableFrom(typeof(TBody)))
        {
            return DecodeProtobuf<TBody>(body);
        }

        if (HasMessagePackObjectAttribute(typeof(TBody)))
        {
            return body.FromMsgPack<TBody>();
        }

        return body.FromJson<TBody>();
    }

    private static TBody DecodeProtobuf<TBody>(ZlinkStreamEncodedBody body)
    {
        if (body.Codec != ZlinkStreamCodec.Protobuf)
        {
            throw new InvalidOperationException($"Stream body codec is {body.Codec}, not Protobuf.");
        }

        var message = ProtobufFactories.GetOrAdd(typeof(TBody), CreateProtobufFactory)();

        message.MergeFrom(body.Body.Span);
        return (TBody)message;
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
