using Google.Protobuf;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Calls;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Runtime;

namespace Systems.Zlink.Stream.Connector.Protobuf;

public static class ZlinkStreamProtobufConnectorExtensions
{
    public static ZlinkStreamProtobufSendBuilder Send<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
        where TBody : IMessage<TBody>
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamProtobufSendBuilder(connector.Send(body.ToProto()));
    }

    public static ZlinkStreamProtobufRequestBuilder Request<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
        where TBody : IMessage<TBody>
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamProtobufRequestBuilder(connector.Request(body.ToProto()));
    }

    public static IDisposable On<TBody>(
        this ZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
        where TBody : IMessage<TBody>, new()
    {
        ArgumentNullException.ThrowIfNull(connector);
        var resolver = connector.Options.NameResolver ?? new ZlinkStreamPacketNameResolver();
        return connector.On(resolver.Resolve(typeof(TBody)), handler);
    }

    public static IDisposable On<TBody>(
        this ZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
        where TBody : IMessage<TBody>, new()
    {
        ArgumentNullException.ThrowIfNull(connector);
        ArgumentNullException.ThrowIfNull(handler);
        return connector.On(name, (message, cancellationToken) =>
        {
            var body = message.Body.FromProto<TBody>();
            return handler(new ZlinkStreamMessage<TBody>(message.Name, message.Metadata, body), cancellationToken);
        });
    }
}

public sealed class ZlinkStreamProtobufSendBuilder
{
    private readonly ZlinkStreamSendBuilder _inner;

    internal ZlinkStreamProtobufSendBuilder(ZlinkStreamSendBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamProtobufSendBuilder WithPacketName(string name)
    {
        _inner.WithPacketName(name);
        return this;
    }

    public ZlinkStreamProtobufSendBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamProtobufSendBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamProtobufSendBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
        => _inner.Async(cancellationToken);
}

public sealed class ZlinkStreamProtobufRequestBuilder
{
    private readonly ZlinkStreamRequestBuilder _inner;

    internal ZlinkStreamProtobufRequestBuilder(ZlinkStreamRequestBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamProtobufRequestBuilder WithPacketName(string name)
    {
        _inner.WithPacketName(name);
        return this;
    }

    public ZlinkStreamProtobufRequestBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamProtobufRequestBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamProtobufRequestBuilder WithTimeout(TimeSpan timeout)
    {
        _inner.WithTimeout(timeout);
        return this;
    }

    public ZlinkStreamProtobufRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        where TReply : IMessage<TReply>, new()
    {
        var reply = await _inner.Async(cancellationToken).ConfigureAwait(false);
        return reply.FromProto<TReply>();
    }

    public void Async(Action<ZlinkStreamResult> callback)
        => _inner.Async(callback);

    public void Async<TReply>(Action<ZlinkStreamResult<TReply>> callback)
        where TReply : IMessage<TReply>, new()
    {
        ArgumentNullException.ThrowIfNull(callback);
        _inner.Async(result =>
        {
            if (!result.IsSuccess)
            {
                callback(ZlinkStreamResult<TReply>.Failure(result.Error!));
                return;
            }

            try
            {
                callback(ZlinkStreamResult<TReply>.Success(result.Value!.FromProto<TReply>()));
            }
            catch (Exception ex)
            {
                callback(ZlinkStreamResult<TReply>.Failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "Protobuf reply decode failed.",
                    ex)));
            }
        });
    }
}
