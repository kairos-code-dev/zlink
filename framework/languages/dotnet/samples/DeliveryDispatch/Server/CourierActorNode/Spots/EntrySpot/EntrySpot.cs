using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;

internal sealed class CourierEntrySpot(
    IZLinkEntrySpotContext context,
    ActorDirectory actors,
    IZLinkActorManager actorManager,
    ILogger<CourierEntrySpot> logger) : IZLinkEntrySpot<CourierActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public async ValueTask OnCreateActorAsync(
        CourierActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        var actorRef = await actorManager.FindAsync(actor.ActorId, cancellationToken)
                       ?? throw new InvalidOperationException(
                           $"Courier actor ref is not available. actor={actor.ActorId}");
        actors.Register(actor, actorRef);
        logger.LogInformation(
            "deliverydispatch courier-entry: actor created courier={CourierId} node={NodeRid}",
            actor.ActorId,
            actorRef.NodeRid);
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CourierActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }
}
