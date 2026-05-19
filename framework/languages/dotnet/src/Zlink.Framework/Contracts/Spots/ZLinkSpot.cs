namespace Zlink.Framework.Contracts.Spots;

public enum ZLinkSpotActorLifecycleKind
{
    Joined = 1,
    Left = 2
}

public sealed record ZLinkSpotActorLifecycleInfo(
    ZLinkSpotActorLifecycleKind Kind,
    string ActorId,
    RoutingId? PreviousNodeRid,
    RoutingId? PreviousSpotRid,
    RoutingId? CurrentNodeRid,
    RoutingId? CurrentSpotRid,
    string? PreviousSpotName,
    string? CurrentSpotName,
    bool PreviousIsEntrySpot,
    bool CurrentIsEntrySpot,
    ulong CommitEpoch)
{
    public string? PreviousActorId => PreviousSpotRid is null ? null : ActorId;

    public string? CurrentActorId => CurrentSpotRid is null ? null : ActorId;

    public ulong JoinEpoch => CommitEpoch;

    public uint Flags { get; init; }
}

public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }

    void Configure()
    {
    }

    ValueTask OnCreateAsync(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnActorJoinedAsync(
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    ValueTask OnActorLeftAsync(
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkActorHandlerRegistry
{
    void AddActorPacket<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorPacket<THandler, TActor>(string packetName)
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;
}

public interface IZLinkSpotContext : IZLinkActorHandlerRegistry
{
    RoutingId SpotRid { get; }

    RoutingId NodeRid { get; }

    string SpotName { get; }

    void AddPacket<THandler>()
        where THandler : class;

    void AddSubscribe<THandler>(string topic)
        where THandler : class;

    void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor;

    ValueTask JoinActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken = default)
        where THandler : class;
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

public interface IZLinkEntrySpotContext : IZLinkActorHandlerRegistry
{
    RoutingId NodeRid { get; }
}

public interface IZLinkSpotActorSendHandler<TSpot, TActor, in TMessage>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorRequestHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorJoinedHandler<TSpot, TActor>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLeftHandler<TSpot, TActor>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TActor, in TMessage>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorRequestHandler<TActor, in TRequest, TReply>
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorJoinedHandler<TActor>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorLeftHandler<TActor>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken);
}
