using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;

namespace Systems.Zlink.Stream.Connector.Json;

public static class ZlinkStreamJsonConnectorExtensions
{
    public static ZlinkStreamJsonSendBuilder Send<TBody>(
        this IZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamJsonSendBuilder(connector.Send(body.ToJson()));
    }

    public static ZlinkStreamJsonRequestBuilder Request<TBody>(
        this IZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamJsonRequestBuilder(connector.Request(body.ToJson()));
    }

    public static IDisposable On<TBody>(
        this IZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        var resolver = connector.Options.NameResolver ?? ZlinkStreamDefaultCodecs.PacketNameResolver();
        return connector.On(resolver.Resolve(typeof(TBody)), handler);
    }

    public static IDisposable On<TBody>(
        this IZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        ArgumentNullException.ThrowIfNull(handler);
        return connector.On(name, (message, cancellationToken) =>
        {
            var body = message.Body.FromJson<TBody>();
            return handler(new ZlinkStreamMessage<TBody>(message.Name, message.Metadata, body), cancellationToken);
        });
    }
}

public sealed class ZlinkStreamJsonSendBuilder
{
    private readonly IZlinkStreamSendCall _inner;

    internal ZlinkStreamJsonSendBuilder(IZlinkStreamSendCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamJsonSendBuilder PacketName(string name)
    {
        _inner.PacketName(name);
        return this;
    }

    public ZlinkStreamJsonSendBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamJsonSendBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamJsonSendBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
        => _inner.Submit(cancellationToken);
}

public sealed class ZlinkStreamJsonRequestBuilder
{
    private readonly IZlinkStreamRequestCall _inner;

    internal ZlinkStreamJsonRequestBuilder(IZlinkStreamRequestCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamJsonRequestBuilder PacketName(string name)
    {
        _inner.PacketName(name);
        return this;
    }

    public ZlinkStreamJsonRequestBuilder Metadata(string key, string value)
    {
        _inner.Metadata(key, value);
        return this;
    }

    public ZlinkStreamJsonRequestBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _inner.Metadata(metadata);
        return this;
    }

    public ZlinkStreamJsonRequestBuilder Timeout(TimeSpan timeout)
    {
        _inner.Timeout(timeout);
        return this;
    }

    public ZlinkStreamJsonRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var reply = await _inner.SubmitAsync(cancellationToken).ConfigureAwait(false);
        return reply.FromJson<TReply>();
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
                callback(ZlinkStreamResult<TReply>.Success(result.Value!.FromJson<TReply>()));
            }
            catch (Exception ex)
            {
                callback(ZlinkStreamResult<TReply>.Failure(new ZlinkStreamError(
                    ZlinkStreamErrorCode.UserCallbackFailed,
                    "JSON reply decode failed.",
                    ex)));
            }
        });
    }
}
