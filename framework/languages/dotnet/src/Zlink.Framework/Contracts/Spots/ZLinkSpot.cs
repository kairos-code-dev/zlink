namespace Zlink.Framework.Contracts.Spots;

public readonly record struct ZLinkSpotActorJoinResult(bool Accepted, ZLinkMessage? Reply)
{
    public static ZLinkSpotActorJoinResult Accept(ZLinkMessage? reply = null)
    {
        var result = ZLinkSpotAcceptRejectResult.Accept(reply);
        return new ZLinkSpotActorJoinResult(result.Accepted, result.Reply);
    }

    public static ZLinkSpotActorJoinResult Accept<TReply>(TReply reply)
    {
        var result = ZLinkSpotAcceptRejectResult.Accept(reply);
        return new ZLinkSpotActorJoinResult(result.Accepted, result.Reply);
    }

    public static ZLinkSpotActorJoinResult Reject(ZLinkMessage? reply = null)
    {
        var result = ZLinkSpotAcceptRejectResult.Reject(reply);
        return new ZLinkSpotActorJoinResult(result.Accepted, result.Reply);
    }

    public static ZLinkSpotActorJoinResult Reject<TReply>(TReply reply)
    {
        var result = ZLinkSpotAcceptRejectResult.Reject(reply);
        return new ZLinkSpotActorJoinResult(result.Accepted, result.Reply);
    }
}

public interface IZLinkActorTransferAdapter<TActor>
    where TActor : IZLinkActor
{
    ValueTask<ZLinkMessage> TransferOutAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask<TActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken);
}

public sealed class ZLinkSpotActorReplyOptions
{
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    internal IReadOnlyDictionary<string, string> MetadataValues => _metadata;

    internal bool CompressPayload { get; private set; }

    internal ZLinkSpotActorReplyOptionsSnapshot CreateSnapshot()
    {
        return new ZLinkSpotActorReplyOptionsSnapshot(
            _metadata.Count == 0
                ? new Dictionary<string, string>(StringComparer.Ordinal)
                : new Dictionary<string, string>(_metadata, StringComparer.Ordinal),
            CompressPayload);
    }

    public ZLinkSpotActorReplyOptions Metadata(string key, string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        ArgumentNullException.ThrowIfNull(value);
        _metadata[key] = value;
        return this;
    }

    public ZLinkSpotActorReplyOptions Compress(bool enabled = true)
    {
        CompressPayload = enabled;
        return this;
    }
}

public sealed class ZLinkSpotActorSendContext : IZLinkHandlerContext
{
    internal ZLinkSpotActorSendContext(
        string meshName,
        string packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        PacketName = packetName;
        ContentType = contentType;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public string MeshName { get; }

    public string? ChannelName => null;

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkSpotActorRequestContext : IZLinkHandlerContext
{
    internal ZLinkSpotActorRequestContext(
        string meshName,
        string packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        PacketName = packetName;
        ContentType = contentType;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        Reply = new ZLinkSpotActorReplyOptions();
    }

    public string MeshName { get; }

    public string? ChannelName => null;

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }

    public ZLinkSpotActorReplyOptions Reply { get; }
}

public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }

    void Configure()
    {
    }

    ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor
{
    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken);

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask OnLeaveActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSpot<TActor> : IZLinkSpot, IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor;

public interface IZLinkActorHandlerRegistry
{
    void AddHandler<THandler>()
        where THandler : class;

    void AddHandler<THandler>(string packetName)
        where THandler : class;

    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;
}

public interface IZLinkSpotHandlerRegistry : IZLinkActorHandlerRegistry
{
    void AddPacket<THandler>()
        where THandler : class;

    void AddSubscribe<THandler>(string topic)
        where THandler : class;
}

public interface IZLinkSpotOutbound
{
    /// <summary>
    /// Sends through the current address held by the spot handle. The framework
    /// refreshes the handle from location updates, but does not retry a one-way
    /// send because delivery may already have occurred.
    /// </summary>
    IZLinkSendCall SendToSpot<TMessage>(
        SpotHandle target,
        TMessage message);

    /// <summary>
    /// Requests through the current address held by the spot handle. If the
    /// address is invalidated during the request, the framework refreshes the
    /// handle and retries once when doing so cannot duplicate a completed call.
    /// </summary>
    IZLinkRequestCall RequestToSpot<TRequest>(
        SpotHandle target,
        TRequest request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);

    IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
        TRequest request);
}

/// <summary>
/// Sends and requests through resolved Spot handles outside a Spot callback.
/// </summary>
public interface IZLinkSpotClient
{
    IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message);

    IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request);
}

public interface IZLinkSpotCommonContext
{
    RoutingId SpotRid { get; }

    RoutingId NodeRid { get; }

    IZLinkSpotHandlerRegistry Handlers { get; }

    IZLinkSpotOutbound Outbound { get; }

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;

    IZLinkWorkerCall<TResult> RunCpuWorker<TResult>(
        Func<CancellationToken, TResult> work);

    IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
        Func<CancellationToken, ValueTask<TResult>> work);
}

public interface IZLinkSpotContext : IZLinkSpotCommonContext
{
    ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkEntrySpot
{
    IZLinkEntrySpotContext Context { get; }

    void Configure()
    {
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkEntrySpot<TActor> : IZLinkEntrySpot, IZLinkSpotActorLifecycle<TActor>
    where TActor : IZLinkActor
{
    ValueTask OnCreateActorAsync(
        TActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkEntrySpotContext : IZLinkSpotCommonContext
{
    ValueTask DestroyActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotActorSendHandler<TSpot, TActor, in TMessage>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorRequestHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, in TMessage>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, in TRequest, TReply>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request,
        CancellationToken cancellationToken);
}
