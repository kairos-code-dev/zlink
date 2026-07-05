using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.Dispatch;

internal sealed class CourierOfferPort(
    SampleTopology topology,
    IZLinkRouteClient routes)
{
    public async ValueTask<DispatchOfferAttempt> OfferAsync(
        AssignDeliveryMsg delivery,
        string courierId,
        CancellationToken cancellationToken)
    {
        var placement = topology.CourierPlacement(courierId);
        var address = new ZLinkSpotAddress(placement.NodeRid, placement.NodeRid);
        var found = await DispatchRouteClient.RequestAsync<FindCourierActorReq, FindCourierActorRes>(
            routes,
            SampleNames.CourierActorDiscovery,
            address,
            new FindCourierActorReq(courierId),
            cancellationToken);
        if (found.Actor is null)
        {
            return DispatchOfferAttempt.NotDelivered(delivery.DeliveryId, courierId, "courier is not bound");
        }

        var response = await DispatchRouteClient.RequestAsync<OfferDeliveryReq, OfferDeliveryRes>(
            routes,
            SampleNames.CourierActorDiscovery,
            new ZLinkSpotAddress(found.Actor.NodeRid, found.Actor.NodeRid),
            new OfferDeliveryReq(courierId, delivery.DeliveryId, delivery.PickupAddress, delivery.DropoffAddress),
            cancellationToken,
            SampleTimings.OfferRequestTimeout);
        return new DispatchOfferAttempt(response, Delivered: true);
    }
}

internal sealed class DeliveryStatusPublisher(IZLinkChannelClient channels)
{
    public async ValueTask PublishAsync(
        AssignDeliveryMsg delivery,
        DeliveryStatus status,
        string? courierId,
        CancellationToken cancellationToken)
    {
        _ = await DispatchChannelClient.RequestAsync<DeliveryStatusChangedReq, DeliveryStatusChangedRes>(
            channels,
            SampleNames.TrackingRouteChannel,
            new DeliveryStatusChangedReq(
                delivery.DeliveryId,
                delivery.CustomerId,
                status,
                courierId,
                DateTimeOffset.UtcNow),
            cancellationToken);
    }
}

internal static class DispatchChannelClient
{
    public static async ValueTask<TRes> RequestAsync<TReq, TRes>(
        IZLinkChannelClient channels,
        string channelName,
        TReq request,
        CancellationToken cancellationToken,
        TimeSpan? timeout = null)
    {
        var call = channels.RequestToChannel(channelName, request);
        if (timeout is { } value)
        {
            call = call.Timeout(value);
        }

        return await call.Async<TRes>(cancellationToken);
    }
}

internal static class DispatchRouteClient
{
    public static async ValueTask<TRes> RequestAsync<TReq, TRes>(
        IZLinkRouteClient routes,
        string routeChannelName,
        ZLinkSpotAddress address,
        TReq request,
        CancellationToken cancellationToken,
        TimeSpan? timeout = null)
    {
        var call = routes.RequestToSpot(routeChannelName, address, request)
            .PacketName(typeof(TReq).Name);
        if (timeout is { } value)
        {
            call = call.Timeout(value);
        }

        return await call.Async<TRes>(cancellationToken);
    }
}
