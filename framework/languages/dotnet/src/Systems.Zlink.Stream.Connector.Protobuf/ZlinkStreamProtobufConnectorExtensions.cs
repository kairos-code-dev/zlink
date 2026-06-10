using Google.Protobuf;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;

namespace Systems.Zlink.Stream.Connector.Protobuf;

public static class ZlinkStreamProtobufConnectorExtensions
{
    public static ZlinkStreamProtobufSendBuilder Send<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload)
        where TPayload : IMessage<TPayload>
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamProtobufSendBuilder(connector.Send(payload.ToProto()));
    }

    public static ZlinkStreamProtobufRequestBuilder Request<TPayload>(
        this IZlinkStreamConnector connector,
        TPayload payload)
        where TPayload : IMessage<TPayload>
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamProtobufRequestBuilder(connector.Request(payload.ToProto()));
    }

    public static IDisposable On<TPayload>(
        this IZlinkStreamConnector connector,
        Func<ZlinkStreamMessage<TPayload>, CancellationToken, ValueTask> handler)
        where TPayload : IMessage<TPayload>, new()
    {
        ArgumentNullException.ThrowIfNull(connector);
        return connector.On(connector.Options.NameResolver.Resolve(typeof(TPayload)), handler);
    }

    public static IDisposable On<TPayload>(
        this IZlinkStreamConnector connector,
        string name,
        Func<ZlinkStreamMessage<TPayload>, CancellationToken, ValueTask> handler)
        where TPayload : IMessage<TPayload>, new()
    {
        ArgumentNullException.ThrowIfNull(connector);
        ArgumentNullException.ThrowIfNull(handler);
        return connector.On(name, (message, cancellationToken) =>
        {
            var payload = message.Payload.FromProto<TPayload>();
            return handler(new ZlinkStreamMessage<TPayload>(message.Name, message.Metadata, payload), cancellationToken);
        });
    }

    public static ZlinkStreamProtobufWaitBuilder<TPayload> WaitFor<TPayload>(
        this IZlinkStreamConnector connector,
        string name)
        where TPayload : IMessage<TPayload>, new()
    {
        ArgumentNullException.ThrowIfNull(connector);
        return new ZlinkStreamProtobufWaitBuilder<TPayload>(connector.WaitFor(name));
    }

    public static ZlinkStreamProtobufWaitBuilder<TPayload> WaitFor<TPayload>(
        this IZlinkStreamConnector connector)
        where TPayload : IMessage<TPayload>, new()
    {
        ArgumentNullException.ThrowIfNull(connector);
        return WaitFor<TPayload>(
            connector,
            connector.Options.NameResolver.Resolve(typeof(TPayload)));
    }

    private static ZlinkStreamMessage<TPayload> Decode<TPayload>(
        ZlinkStreamMessage<ZlinkStreamEncodedPayload> message)
        where TPayload : IMessage<TPayload>, new()
    {
        var payload = message.Payload.FromProto<TPayload>();
        return new ZlinkStreamMessage<TPayload>(message.Name, message.Metadata, payload);
    }
}

public sealed class ZlinkStreamProtobufWaitBuilder<TPayload>
    where TPayload : IMessage<TPayload>, new()
{
    private readonly IZlinkStreamWaitCall _inner;

    internal ZlinkStreamProtobufWaitBuilder(IZlinkStreamWaitCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamProtobufWaitBuilder<TPayload> Timeout(TimeSpan timeout)
    {
        _inner.Timeout(timeout);
        return this;
    }

    public ZlinkStreamProtobufWaitBuilder<TPayload> Where(Func<ZlinkStreamMessage<TPayload>, bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        _inner.Where(message => predicate(new ZlinkStreamMessage<TPayload>(
            message.Name,
            message.Metadata,
            message.Payload.FromProto<TPayload>())));
        return this;
    }

    public async ValueTask<ZlinkStreamMessage<TPayload>> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        var message = await _inner.SubmitAsync(cancellationToken).ConfigureAwait(false);
        return new ZlinkStreamMessage<TPayload>(
            message.Name,
            message.Metadata,
            message.Payload.FromProto<TPayload>());
    }
}

public sealed class ZlinkStreamProtobufSendBuilder
{
    private readonly IZlinkStreamSendCall _inner;

    internal ZlinkStreamProtobufSendBuilder(IZlinkStreamSendCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamProtobufSendBuilder PacketName(string name)
    {
        _inner.PacketName(name);
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

    public ValueTask SubmitAsync(CancellationToken cancellationToken = default)
        => _inner.SubmitAsync(cancellationToken);
}

public sealed class ZlinkStreamProtobufRequestBuilder
{
    private readonly IZlinkStreamRequestCall _inner;

    internal ZlinkStreamProtobufRequestBuilder(IZlinkStreamRequestCall inner)
    {
        _inner = inner;
    }

    public ZlinkStreamProtobufRequestBuilder PacketName(string name)
    {
        _inner.PacketName(name);
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

    public ZlinkStreamProtobufRequestBuilder Timeout(TimeSpan timeout)
    {
        _inner.Timeout(timeout);
        return this;
    }

    public ZlinkStreamProtobufRequestBuilder Compress()
    {
        _inner.Compress();
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
        where TReply : IMessage<TReply>, new()
    {
        var reply = await _inner.SubmitAsync(cancellationToken).ConfigureAwait(false);
        return reply.FromProto<TReply>();
    }

    public void Submit(Action<ZlinkStreamResult> callback)
        => _inner.Submit(callback);

    public void Submit<TReply>(Action<ZlinkStreamResult<TReply>> callback)
        where TReply : IMessage<TReply>, new()
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
