using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierSpot;

internal sealed class CourierEntrySpot(
    IZLinkEntrySpotContext context,
    CourierActorDirectory actors) : IZLinkEntrySpot<CourierActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddActorPacket<CourierDecisionActorHandler, CourierActor>(nameof(CourierDecision));
        Context.Handlers.AddActorPacket<CourierSessionAttachedActorHandler, CourierActor>(nameof(CourierSessionAttached));
    }

    public ValueTask OnCreateActorAsync(
        CourierActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        _ = createRequest;
        cancellationToken.ThrowIfCancellationRequested();
        actors.Register(actor);
        Console.Error.WriteLine(
            $"deliverydispatch courier-entry: actor created courier={actor.ActorId} node={Context.NodeRid}");
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CourierActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = actor;
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }
}

internal sealed class CourierDecisionActorHandler
    : IZLinkEntrySpotActorSendHandler<CourierEntrySpot, CourierActor, CourierDecision>
{
    public ValueTask HandleAsync(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorSendContext context,
        CourierDecision message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Complete(message);
        Console.Error.WriteLine(
            $"deliverydispatch courier-actor: decision delivery={message.DeliveryId} courier={actor.ActorId} accepted={message.Accepted}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class CourierSessionAttachedActorHandler
    : IZLinkEntrySpotActorSendHandler<CourierEntrySpot, CourierActor, CourierSessionAttached>
{
    public ValueTask HandleAsync(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorSendContext context,
        CourierSessionAttached message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        Console.Error.WriteLine(
            $"deliverydispatch courier-actor: session attached courier={message.CourierId} actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }
}
