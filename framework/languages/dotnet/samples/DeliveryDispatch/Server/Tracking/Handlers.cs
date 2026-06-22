using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Tracking;

internal sealed class EnsureCustomerActorHandler(
    IZLinkActorManager actors,
    SampleTopology topology)
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
            cancellationToken);
        using var entryJoinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        var joined = await actor.Context.JoinEntrySpot(
                topology.TrackingSpotNodeRid,
                entryJoinRequest)
            .Async(cancellationToken);
        return new CustomerActorEnsured(
            request.CustomerId,
            new ActorRefSnapshot(
                joined.Actor.NodeRid.ToString(),
                joined.Actor.ActorId,
                joined.Actor.Generation));
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
            new DeliverySpotCreate(request.DeliveryId).ToJson(),
            cancellationToken);
        var actor = await actors.GetOrCreateAsync(
            request.CustomerId,
            SampleNames.CustomerActorType,
            cancellationToken);
        await actor.Context.JoinSpot(
                RoutingId.From(request.DeliveryId),
                new DeliverySpotJoin(request.DeliveryId, request.CustomerId).ToJson())
            .Async(cancellationToken);
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
            new DeliverySpotCreate(request.DeliveryId).ToJson(),
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
