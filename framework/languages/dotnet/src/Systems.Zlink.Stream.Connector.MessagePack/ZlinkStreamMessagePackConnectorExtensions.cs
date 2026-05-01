using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Calls;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Runtime;

namespace Systems.Zlink.Stream.Connector.MessagePack;

public static class ZlinkStreamMessagePackConnectorExtensions
{
    public static ZlinkStreamMessagePackSendBuilder Send<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamMessagePackSendBuilder(connector.Send(body.ToMsgPack()));
    }

    public static ZlinkStreamMessagePackRequestBuilder Request<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamMessagePackRequestBuilder(connector.Request(body.ToMsgPack()));
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
            var body = message.Body.FromMsgPack<TBody>();
            return handler(new ZlinkStreamMessage<TBody>(message.Name, message.Metadata, body), cancellationToken);
        });
    }
}

public sealed class ZlinkStreamMessagePackSendBuilder
{
    private readonly ZlinkStreamSendBuilder _inner;

    internal ZlinkStreamMessagePackSendBuilder(ZlinkStreamSendBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamMessagePackSendBuilder WithPacketName(string name)
    {
        _inner.WithPacketName(name);
        return this;
    }

    public ZlinkStreamMessagePackSendBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamMessagePackSendBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamMessagePackSendBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
        => _inner.Async(cancellationToken);
}

public sealed class ZlinkStreamMessagePackRequestBuilder
{
    private readonly ZlinkStreamRequestBuilder _inner;

    internal ZlinkStreamMessagePackRequestBuilder(ZlinkStreamRequestBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamMessagePackRequestBuilder WithPacketName(string name)
    {
        _inner.WithPacketName(name);
        return this;
    }

    public ZlinkStreamMessagePackRequestBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamMessagePackRequestBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamMessagePackRequestBuilder WithTimeout(TimeSpan timeout)
    {
        _inner.WithTimeout(timeout);
        return this;
    }

    public ZlinkStreamMessagePackRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var reply = await _inner.Async(cancellationToken).ConfigureAwait(false);
        return reply.FromMsgPack<TReply>();
    }

    public void Async(Action<ZlinkStreamResult> callback)
        => _inner.Async(callback);

    public void Async<TReply>(Action<ZlinkStreamResult<TReply>> callback)
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
                callback(ZlinkStreamResult<TReply>.Success(result.Value!.FromMsgPack<TReply>()));
            }
            catch (Exception ex)
            {
                callback(ZlinkStreamResult<TReply>.Failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "MessagePack reply decode failed.",
                    ex)));
            }
        });
    }
}
