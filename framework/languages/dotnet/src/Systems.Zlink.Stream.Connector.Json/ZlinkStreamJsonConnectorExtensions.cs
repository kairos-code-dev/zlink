using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Calls;
using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Runtime;

namespace Systems.Zlink.Stream.Connector.Json;

public static class ZlinkStreamJsonConnectorExtensions
{
    public static ZlinkStreamJsonSendBuilder Send<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamJsonSendBuilder(connector.Send(body.ToJson()));
    }

    public static ZlinkStreamJsonRequestBuilder Request<TBody>(
        this ZlinkStreamConnector connector,
        TBody body)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamJsonRequestBuilder(connector.Request(body.ToJson()));
    }

    public static IDisposable On<TBody>(
        this ZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        var resolver = connector.Options.NameResolver ?? new ZlinkStreamMessageNameResolver();
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
            var body = message.Body.FromJson<TBody>();
            return handler(new ZlinkStreamMessage<TBody>(message.Name, message.Metadata, body), cancellationToken);
        });
    }
}

public sealed class ZlinkStreamJsonSendBuilder
{
    private readonly ZlinkStreamSendBuilder _inner;

    internal ZlinkStreamJsonSendBuilder(ZlinkStreamSendBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamJsonSendBuilder WithMessageName(string name)
    {
        _inner.WithMessageName(name);
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

    public ZlinkStreamJsonSendBuilder WithTimeout(TimeSpan timeout)
    {
        _inner.WithTimeout(timeout);
        return this;
    }

    public ZlinkStreamJsonSendBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public void Exec(CancellationToken cancellationToken = default)
        => _inner.Exec(cancellationToken);
}

public sealed class ZlinkStreamJsonRequestBuilder
{
    private readonly ZlinkStreamRequestBuilder _inner;

    internal ZlinkStreamJsonRequestBuilder(ZlinkStreamRequestBuilder inner)
    {
        _inner = inner;
    }

    public ZlinkStreamJsonRequestBuilder WithMessageName(string name)
    {
        _inner.WithMessageName(name);
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

    public ZlinkStreamJsonRequestBuilder WithTimeout(TimeSpan timeout)
    {
        _inner.WithTimeout(timeout);
        return this;
    }

    public ZlinkStreamJsonRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> ExecAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var reply = await _inner.ExecAsync(cancellationToken).ConfigureAwait(false);
        return reply.FromJson<TReply>();
    }

    public void Exec(Action<ZlinkStreamResult> callback)
        => _inner.Exec(callback);

    public void Exec<TReply>(Action<ZlinkStreamResult<TReply>> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        _inner.Exec(result =>
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
