using DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot.Handlers;
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
        Context.Handlers.AddActorRequest<SubscribeDeliveryActorHandler, CustomerActor>(nameof(SubscribeDelivery));
    }

    public SubscribeDeliveryAccepted SubscribeCustomer(
        string customerId,
        string deliveryId)
    {
        actors.Subscribe(customerId, deliveryId);
        logger.LogInformation(
            "deliverydispatch customer-entry: subscribed customer={CustomerId} delivery={DeliveryId}",
            customerId,
            deliveryId);
        return new SubscribeDeliveryAccepted(deliveryId);
    }

    public ValueTask OnCreateActorAsync(
        CustomerActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        _ = createRequest;
        cancellationToken.ThrowIfCancellationRequested();
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
        _ = actor;
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }
}
