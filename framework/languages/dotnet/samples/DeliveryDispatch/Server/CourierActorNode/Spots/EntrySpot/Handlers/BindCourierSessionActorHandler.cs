using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot.Handlers;

internal sealed class BindCourierSessionActorHandler(
    IZLinkActorManager actorManager,
    ILogger<BindCourierSessionActorHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<CourierEntrySpot, CourierActor, BindCourierReq, BindCourierRes>
{
    public async ValueTask<BindCourierRes> HandleAsync(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorRequestContext context,
        BindCourierReq message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        var actorRef = await actorManager.FindAsync(actor.ActorId, cancellationToken)
                       ?? throw new InvalidOperationException(
                           $"Courier actor ref is not available. actor={actor.ActorId}");
        logger.LogInformation(
            "deliverydispatch courier-actor: session bound courier={CourierId} actor={ActorId} node={NodeRid} session={SessionRoute}",
            message.CourierId,
            actor.ActorId,
            actorRef.NodeRid,
            message.SessionRoute);
        return new BindCourierRes(
            message.CourierId,
            ActorRefSnapshot.From(actorRef),
            message.SessionRoute);
    }
}
