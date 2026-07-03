using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot;

internal sealed class CustomerEntrySpot(
    IZLinkEntrySpotContext context,
    CustomerActorDirectory actors,
    ILogger<CustomerEntrySpot> logger) : IZLinkEntrySpot<CustomerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public void Configure()
    {
    }

    public async ValueTask PushStatusAsync(
        DeliveryStatusUpdatedMsg status,
        CancellationToken cancellationToken)
    {
        await actors.PushAsync(status, cancellationToken);
    }

    public ValueTask OnCreateActorAsync(
        CustomerActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        actors.Register(actor);
        logger.LogInformation(
            "deliverydispatch customer-entry: actor created customer={ActorId}",
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }
}
