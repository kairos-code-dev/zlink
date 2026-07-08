using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot;

internal sealed class CustomerEntrySpot(
    IZLinkEntrySpotContext context,
    CustomerActorDirectory actors,
    IZLinkActorManager actorManager,
    ILogger<CustomerEntrySpot> logger) : IZLinkEntrySpot<CustomerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public async ValueTask PushStatusAsync(
        DeliveryStatusUpdatedMsg status,
        CancellationToken cancellationToken)
    {
        await actors.PushAsync(status, cancellationToken);
    }

    public async ValueTask OnCreateActorAsync(
        CustomerActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        var actorRef = await actorManager.FindAsync(actor.ActorId, cancellationToken)
                       ?? throw new InvalidOperationException(
                           $"Customer actor ref is not available. actor={actor.ActorId}");
        actors.Register(actor, actorRef);
        logger.LogInformation(
            "deliverydispatch customer-entry: actor created customer={ActorId} node={NodeRid}",
            actor.ActorId,
            actorRef.NodeRid);
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }
}
