using DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot.Handlers;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;

internal sealed class CourierEntrySpot(
    IZLinkEntrySpotContext context,
    ActorDirectory actors,
    ILogger<CourierEntrySpot> logger) : IZLinkEntrySpot<CourierActor>
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
        logger.LogInformation(
            "deliverydispatch courier-entry: actor created courier={CourierId} node={NodeRid}",
            actor.ActorId,
            Context.NodeRid);
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
