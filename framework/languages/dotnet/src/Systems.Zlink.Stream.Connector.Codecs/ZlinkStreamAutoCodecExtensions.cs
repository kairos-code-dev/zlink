using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Systems.Zlink.Stream.Connector.Codecs;

public static class ZlinkStreamAutoCodecExtensions
{
    public static ZlinkStreamAutoCodecSendBuilder Send<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamAutoCodecSendBuilder(connector.Send(ZlinkStreamAutoCodecSelector.Encode(payload)));
    }

    public static ZlinkStreamAutoCodecRequestBuilder Request<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamAutoCodecRequestBuilder(connector.Request(ZlinkStreamAutoCodecSelector.Encode(payload)));
    }

    public static IDisposable On<TPayload>(
        this IZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TPayload>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return connector.On(connector.Options.NameResolver.Resolve(typeof(TPayload)), handler);
    }

    public static IDisposable On<TPayload>(
        this IZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TPayload>, CancellationToken, ValueTask> handler)
    {
        ArgumentNullException.ThrowIfNull(connector);
        ArgumentNullException.ThrowIfNull(handler);
        return connector.On(name, (message, cancellationToken) =>
        {
            var payload = ZlinkStreamAutoCodecSelector.Decode<TPayload>(message.Payload);
            return handler(new ZlinkStreamMessage<TPayload>(message.Name, message.Metadata, payload), cancellationToken);
        });
    }

    public static async ValueTask<ZlinkStreamMessage<TPayload>> WaitForAsync<TPayload>(
        this IZlinkStreamConnector connector,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return await WaitForAsync<TPayload>(
                connector,
                connector.Options.NameResolver.Resolve(typeof(TPayload)),
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public static async ValueTask<ZlinkStreamMessage<TPayload>> WaitForAsync<TPayload>(
        this IZlinkStreamConnector connector,
        string name,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(connector);
        var message = await connector.WaitForAsync(name, timeout, cancellationToken)
            .ConfigureAwait(false);
        return Decode<TPayload>(message);
    }

    public static async ValueTask<ZlinkStreamMessage<TPayload>> WaitForAsync<TPayload>(
        this IZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TPayload>, bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return await WaitForAsync(
                connector,
                connector.Options.NameResolver.Resolve(typeof(TPayload)),
                predicate,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public static async ValueTask<ZlinkStreamMessage<TPayload>> WaitForAsync<TPayload>(
        this IZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TPayload>, bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(connector);
        ArgumentNullException.ThrowIfNull(predicate);
        var message = await connector.WaitForAsync(
                name,
                encoded => predicate(Decode<TPayload>(encoded)),
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return Decode<TPayload>(message);
    }

    private static ZlinkStreamMessage<TPayload> Decode<TPayload>(
        ZlinkStreamMessage<ZlinkStreamEncodedPayload> message)
    {
        var payload = ZlinkStreamAutoCodecSelector.Decode<TPayload>(message.Payload);
        return new ZlinkStreamMessage<TPayload>(message.Name, message.Metadata, payload);
    }
}

public sealed class ZlinkStreamAutoCodecSendBuilder
{
    private readonly IZlinkStreamSendCall _inner;

    internal ZlinkStreamAutoCodecSendBuilder(IZlinkStreamSendCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamAutoCodecSendBuilder PacketName(string name)
    {
        _inner.PacketName(name);
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
    private readonly IZlinkStreamRequestCall _inner;

    internal ZlinkStreamAutoCodecRequestBuilder(IZlinkStreamRequestCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamAutoCodecRequestBuilder PacketName(string name)
    {
        _inner.PacketName(name);
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

    public ZlinkStreamAutoCodecRequestBuilder Timeout(TimeSpan timeout)
    {
        _inner.Timeout(timeout);
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
        return ZlinkStreamAutoCodecSelector.Decode<TReply>(reply);
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
                    ZlinkStreamAutoCodecSelector.Decode<TReply>(result.Value!)));
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
