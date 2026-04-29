namespace Zlink.Framework.Spots;

public interface IZLinkSpot
{
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

public interface IZLinkSpotContext
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

internal sealed record ZLinkSpotPacketRegistration(Type HandlerType);

internal sealed record ZLinkSpotSubscriptionRegistration(string Topic, Type HandlerType);

internal sealed record ZLinkSpotActorJoinRegistration(
    Type HandlerType,
    Type ActorType,
    Type RequestType,
    Type ReplyType);
