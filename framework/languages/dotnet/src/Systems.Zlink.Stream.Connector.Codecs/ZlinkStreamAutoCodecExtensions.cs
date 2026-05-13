using Google.Protobuf;
using MessagePack;
using Systems.Zlink.Stream.Connector.Calls;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Json;
using Systems.Zlink.Stream.Connector.MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Protobuf;

namespace Systems.Zlink.Stream.Connector.Codecs;

public static class ZlinkStreamAutoCodecExtensions
{
    public static ZlinkStreamAutoCodecSendBuilder Send<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamAutoCodecSendBuilder(connector.Send(Encode(body)));
    }

    public static ZlinkStreamAutoCodecRequestBuilder Request<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamAutoCodecRequestBuilder(connector.Request(Encode(body)));
    }

    public static IDisposable On<TBody>(
        this ZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        var resolver = connector.Options.NameResolver ?? new ZlinkStreamPacketNameResolver();
        return connector.On(resolver.Resolve(typeof(TBody)), handler);
    }

    public static IDisposable On<TBody>(
        this ZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        ArgumentNullException.ThrowIfNull(handler);
        return connector.On(name, (message, cancellationToken) =>
        {
            var body = Decode<TBody>(message.Body);
            return handler(new ZlinkStreamMessage<TBody>(message.Name, message.Metadata, body), cancellationToken);
        });
    }

    internal static ZlinkStreamEncodedBody Encode<TBody>(TBody body)
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

    internal static TBody Decode<TBody>(ZlinkStreamEncodedBody body)
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

        var instance = Activator.CreateInstance(typeof(TBody));
        if (instance is not IMessage message)
        {
            throw new InvalidOperationException($"{typeof(TBody).FullName} must implement IMessage and have a public parameterless constructor.");
        }

        message.MergeFrom(body.Body.Span);
        return (TBody)message;
    }

    private static bool HasMessagePackObjectAttribute(Type type)
        => type.GetCustomAttributes(typeof(MessagePackObjectAttribute), inherit: true).Length > 0;
}

public sealed class ZlinkStreamAutoCodecSendBuilder
{
    private readonly ZlinkStreamSendBuilder _inner;

    internal ZlinkStreamAutoCodecSendBuilder(ZlinkStreamSendBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamAutoCodecSendBuilder WithPacketName(string name)
    {
        _inner.WithPacketName(name);
        return this;
    }

    public ZlinkStreamAutoCodecSendBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamAutoCodecSendBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamAutoCodecSendBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
        => _inner.Submit(cancellationToken);
}

public sealed class ZlinkStreamAutoCodecRequestBuilder
{
    private readonly ZlinkStreamRequestBuilder _inner;

    internal ZlinkStreamAutoCodecRequestBuilder(ZlinkStreamRequestBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamAutoCodecRequestBuilder WithPacketName(string name)
    {
        _inner.WithPacketName(name);
        return this;
    }

    public ZlinkStreamAutoCodecRequestBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamAutoCodecRequestBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamAutoCodecRequestBuilder WithTimeout(TimeSpan timeout)
    {
        _inner.WithTimeout(timeout);
        return this;
    }

    public ZlinkStreamAutoCodecRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var reply = await _inner.SubmitAsync(cancellationToken).ConfigureAwait(false);
        return ZlinkStreamAutoCodecExtensions.Decode<TReply>(reply);
    }

    public void Submit(Action<ZlinkStreamResult> callback)
        => _inner.Submit(callback);

    public void Submit<TReply>(Action<ZlinkStreamResult<TReply>> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        _inner.Submit(result =>
        {
            if (!result.IsSuccess)
            {
                callback(ZlinkStreamResult<TReply>.Failure(result.Error!));
                return;
            }

            try
            {
                callback(ZlinkStreamResult<TReply>.Success(
                    ZlinkStreamAutoCodecExtensions.Decode<TReply>(result.Value!)));
            }
            catch (Exception ex)
            {
                callback(ZlinkStreamResult<TReply>.Failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "Stream reply decode failed.",
                    ex)));
            }
        });
    }
}
