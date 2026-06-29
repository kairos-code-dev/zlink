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
        Context.Handlers.AddActorRequest<BindCourierSessionActorHandler, CourierActor>(nameof(BindCourierSession));
        Context.Handlers.AddActorPacket<CourierDecisionActorHandler, CourierActor>(nameof(CourierDecision));
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

internal sealed class BindCourierSessionActorHandler
    : IZLinkEntrySpotActorRequestHandler<CourierEntrySpot, CourierActor, BindCourierSession, CourierBound>
{
    public ValueTask<CourierBound> HandleAsync(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorRequestContext context,
        BindCourierSession message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var actorRef = message.Actor
            ?? throw new InvalidOperationException("Courier bind relay requires actor ref.");
        var sessionRoute = message.SessionRoute
            ?? throw new InvalidOperationException("Courier bind relay requires session route.");
        Console.Error.WriteLine(
            $"deliverydispatch courier-actor: session bound courier={message.CourierId} actor={actor.ActorId}");
        return ValueTask.FromResult(new CourierBound(message.CourierId, actorRef, sessionRoute));
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
