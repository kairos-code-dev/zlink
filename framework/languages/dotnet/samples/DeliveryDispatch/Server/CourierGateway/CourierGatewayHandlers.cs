using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.CourierGateway;

[ZLinkHandlerGroup(SampleNames.CourierRouteChannel)]
internal sealed class BindCourierHandler(
    CourierDirectory directory,
    IZLinkRouteClient routes,
    ILogger<BindCourierHandler> logger)
    : IZLinkRequestHandler<BindCourier, CourierBound>
{
    public async ValueTask<CourierBound> HandleAsync(
        BindCourier request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var placement = directory.ChoosePlacement(request.CourierId);
        var ensured = await routes.Request(
                SampleNames.CourierSpotRouteChannel,
                placement.NodeRid,
                new EnsureCourierActor(request.CourierId))
            .PacketName(nameof(EnsureCourierActor))
            .Async<CourierActorEnsured>(cancellationToken);
        var binding = directory.Remember(ensured, request.SessionRoute);
        logger.LogInformation(
            "deliverydispatch courier-gateway: bound courier={CourierId} node={NodeRid} session={SessionRoute}",
            request.CourierId,
            binding.Actor.NodeRid,
            binding.SessionRoute);
        return new CourierBound(request.CourierId, binding.Actor, binding.SessionRoute);
    }
}

[ZLinkHandlerGroup(SampleNames.CourierRouteChannel)]
internal sealed class OfferDeliveryHandler(
    CourierDirectory directory,
    IZLinkRouteClient routes)
    : IZLinkRequestHandler<OfferDelivery, OfferDeliveryResult>
{
    public async ValueTask<OfferDeliveryResult> HandleAsync(
        OfferDelivery request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var binding = directory.Require(request.CourierId);
        return await routes.Request(
                SampleNames.CourierSpotRouteChannel,
                Systems.Zlink.RoutingId.From(binding.Actor.NodeRid),
                request)
            .PacketName(nameof(OfferDelivery))
            .Timeout(SampleTimings.DispatchTimeout)
            .Async<OfferDeliveryResult>(cancellationToken);
    }
}
