using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Tracking;

internal sealed class EnsureCustomerActorHandler(
    IZLinkActorManager actors)
    : IZLinkRequestHandler<EnsureCustomerActor, CustomerActorEnsured>
{
    public async ValueTask<CustomerActorEnsured> HandleAsync(
        EnsureCustomerActor request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
            request.CustomerId,
            SampleNames.CustomerActorType,
            request,
            cancellationToken);

        return new CustomerActorEnsured(
            request.CustomerId,
            new ActorRefSnapshot(
                actor.NodeRid.ToString(),
                actor.ActorId,
                actor.Generation));
    }
}

internal sealed class SubscribeCustomerToDeliveryHandler(
    IZLinkActorManager actors,
    IZLinkSpotManager spots)
    : IZLinkRequestHandler<SubscribeCustomerToDelivery, CustomerDeliverySubscribed>
{
    public async ValueTask<CustomerDeliverySubscribed> HandleAsync(
        SubscribeCustomerToDelivery request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<DeliveryTrackingSpot>(
            RoutingId.From(request.DeliveryId),
            new DeliverySpotCreate(request.DeliveryId),
            cancellationToken);
        var actor = await actors.GetOrCreateAsync(
            request.CustomerId,
            SampleNames.CustomerActorType,
            cancellationToken);
        _ = actor;
        return new CustomerDeliverySubscribed(request.CustomerId, request.DeliveryId);
    }
}

internal sealed class DeliveryStatusChangedHandler(
    IZLinkSpotManager spots,
    DeliverySpotDirectory directory,
    EvidenceStore evidence,
    IZLinkFanoutClient fanout)
    : IZLinkRequestHandler<DeliveryStatusChanged, DeliveryStatusAck>
{
    public async ValueTask<DeliveryStatusAck> HandleAsync(
        DeliveryStatusChanged request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<DeliveryTrackingSpot>(
            RoutingId.From(request.DeliveryId),
            new DeliverySpotCreate(request.DeliveryId),
            cancellationToken);
        evidence.Append(request);
        directory.Require(request.DeliveryId)
            .Record(request);
        await fanout.Publish(
                SampleNames.StatusFanoutChannel,
                request.DeliveryId,
                new DeliveryStatusNotify(
                    request.DeliveryId,
                    request.Status,
                    request.CourierId,
                    request.OccurredAt))
            .Async(cancellationToken);
        Console.Error.WriteLine($"deliverydispatch tracking: status delivery={request.DeliveryId} status={request.Status} courier={request.CourierId}");
        return new DeliveryStatusAck(request.DeliveryId, request.Status);
    }
}
