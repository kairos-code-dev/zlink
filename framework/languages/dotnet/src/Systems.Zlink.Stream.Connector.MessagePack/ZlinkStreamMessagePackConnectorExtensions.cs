using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;

namespace Systems.Zlink.Stream.Connector.MessagePack;

public static class ZlinkStreamMessagePackConnectorExtensions
{
    public static ZlinkStreamMessagePackSendBuilder Send<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamMessagePackSendBuilder(connector.Send(payload.ToMsgPack()));
    }

    public static ZlinkStreamMessagePackRequestBuilder Request<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload)
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamMessagePackRequestBuilder(connector.Request(payload.ToMsgPack()));
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
            var payload = message.Payload.FromMsgPack<TPayload>();
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
        var payload = message.Payload.FromMsgPack<TPayload>();
        return new ZlinkStreamMessage<TPayload>(message.Name, message.Metadata, payload);
    }
}

public sealed class ZlinkStreamMessagePackSendBuilder
{
    private readonly IZlinkStreamSendCall _inner;

    internal ZlinkStreamMessagePackSendBuilder(IZlinkStreamSendCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamMessagePackSendBuilder PacketName(string name)
    {
        _inner.PacketName(name);
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

    public ValueTask Submit(CancellationToken cancellationToken = default)
        => _inner.Submit(cancellationToken);
}

public sealed class ZlinkStreamMessagePackRequestBuilder
{
    private readonly IZlinkStreamRequestCall _inner;

    internal ZlinkStreamMessagePackRequestBuilder(IZlinkStreamRequestCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamMessagePackRequestBuilder PacketName(string name)
    {
        _inner.PacketName(name);
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

    public ZlinkStreamMessagePackRequestBuilder Timeout(TimeSpan timeout)
    {
        _inner.Timeout(timeout);
        return this;
    }

    public ZlinkStreamMessagePackRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var reply = await _inner.SubmitAsync(cancellationToken).ConfigureAwait(false);
        return reply.FromMsgPack<TReply>();
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
