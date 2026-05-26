namespace Zlink.Framework.Contracts.Spots;

public enum ZLinkSpotActorChangeKind
{
    JoinSpot = 1,
    JoinEntrySpot = 2,
    LeaveSpot = 3
}

public sealed record ZLinkSpotActorChangeResult
{
    public ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind kind)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, "SPOT actor change kind is not defined.");
        }

        Kind = kind;
    }

    public ZLinkSpotActorChangeKind Kind { get; }
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

    void AddPostActorJoined<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorLeft<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorDisconnected<THandler, TActor>()
        where THandler : class
        where TActor : IZLinkActor;
}

public interface IZLinkSpotHandlerRegistry : IZLinkActorHandlerRegistry
{
    void AddPacket<THandler>()
        where THandler : class;

    void AddSubscribe<THandler>(string topic)
        where THandler : class;

    void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor;

    void AddActorJoin<THandler>()
        where THandler : class;
}

public interface IZLinkSpotOutboundContext
{
    IZLinkSendCall SendSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall RequestSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);
}

public interface IZLinkSpotContext : IZLinkSpotHandlerRegistry, IZLinkSpotOutboundContext
{
    RoutingId SpotRid { get; }

    RoutingId NodeRid { get; }

    ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
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

public interface IZLinkEntrySpotContext : IZLinkSpotHandlerRegistry, IZLinkSpotOutboundContext
{
    RoutingId SpotRid { get; }

    RoutingId NodeRid { get; }

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;
}

public interface IZLinkSpotActorSendHandler<TSpot, TActor, in TMessage>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
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
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotPostActorJoinedHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorChangeResult result,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorLeftHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorChangeResult result,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorDisconnectedHandler<TSpot, TActor>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TSpot spot,
        TActor actor,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, in TMessage>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
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
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorDisconnectedHandler<TEntrySpot, TActor>
    where TEntrySpot : class, IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        CancellationToken cancellationToken);
}
