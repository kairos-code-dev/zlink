namespace Zlink.Framework.Contracts.Spots;

public readonly record struct ZLinkSpotActorJoinResult(bool Accepted, ZLinkMessage? Reply)
{
    public static ZLinkSpotActorJoinResult Accept(ZLinkMessage? reply = null)
    {
        return new ZLinkSpotActorJoinResult(true, reply);
    }

    public static ZLinkSpotActorJoinResult Accept<TReply>(TReply reply)
    {
        return new ZLinkSpotActorJoinResult(true, ZLinkMessage.From(reply));
    }

    public static ZLinkSpotActorJoinResult Reject(ZLinkMessage? reply = null)
    {
        return new ZLinkSpotActorJoinResult(false, reply);
    }

    public static ZLinkSpotActorJoinResult Reject<TReply>(TReply reply)
    {
        return new ZLinkSpotActorJoinResult(false, ZLinkMessage.From(reply));
    }
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

public sealed class ZLinkSpotActorSendContext : ZLinkHandlerContext
{
    internal ZLinkSpotActorSendContext(
        string? packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(null, packetName, contentType, connectionAborted)
    {
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public ZLinkMessageMetadata Metadata { get; }
}

public sealed class ZLinkSpotActorRequestContext : ZLinkHandlerContext
{
    internal ZLinkSpotActorRequestContext(
        string? packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(null, packetName, contentType, connectionAborted)
    {
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        Reply = new ZLinkSpotActorReplyOptions();
    }

    public ZLinkMessageMetadata Metadata { get; }

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

public interface IZLinkSpot<TActor> : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        TActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject());
    }

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnLeaveActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnDisconnectActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

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

    void AddActorSend<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        AddActorPacket<THandler, TActor>(packetName);
    }

    void AddActorRequest<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor
    {
        AddActorPacket<THandler, TActor>(packetName);
    }
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
    IZLinkSendCall SendToSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall RequestToSpot<TRequest>(
        RoutingId spotRid,
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

public interface IZLinkSpotContext
{
    RoutingId SpotRid { get; }

    RoutingId NodeRid { get; }

    IZLinkSpotHandlerRegistry Handlers { get; }

    IZLinkSpotOutbound Outbound { get; }

    ValueTask leaveActor(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;

    IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work);
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

public interface IZLinkEntrySpot<TActor> : IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask OnCreateActorAsync(
        TActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        TActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Reject());
    }

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnLeaveActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnDisconnectActorAsync(
        TActor actor,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkEntrySpotContext
{
    RoutingId SpotRid { get; }

    RoutingId NodeRid { get; }

    IZLinkSpotHandlerRegistry Handlers { get; }

    IZLinkSpotOutbound Outbound { get; }

    ValueTask DestroyActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;

    IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work);
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