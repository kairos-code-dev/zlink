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
    : IZLinkRequestHandler<BindCourierReq, BindCourierRes>
{
    public async ValueTask<BindCourierRes> HandleAsync(
        BindCourierReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var placement = directory.ChoosePlacement(request.CourierId);
        var ensured = await routes.Request(
                SampleNames.CourierActorNodeRouteChannel,
                placement.NodeRid,
                new EnsureCourierActorReq(request.CourierId))
            .PacketName(nameof(EnsureCourierActorReq))
            .Async<EnsureCourierActorRes>(cancellationToken);
        var binding = directory.Remember(ensured, request.SessionRoute);
        logger.LogInformation(
            "deliverydispatch courier-gateway: bound courier={CourierId} node={NodeRid} session={SessionRoute}",
            request.CourierId,
            binding.Actor.NodeRid,
            binding.SessionRoute);
        return new BindCourierRes(request.CourierId, binding.Actor, binding.SessionRoute);
    }
}

[ZLinkHandlerGroup(SampleNames.CourierRouteChannel)]
internal sealed class OfferDeliveryHandler(
    CourierDirectory directory,
    IZLinkRouteClient routes)
    : IZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes>
{
    public async ValueTask<OfferDeliveryRes> HandleAsync(
        OfferDeliveryReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var binding = directory.Require(request.CourierId);
        return await routes.Request(
                SampleNames.CourierActorNodeRouteChannel,
                Systems.Zlink.RoutingId.From(binding.Actor.NodeRid),
                request)
            .PacketName(nameof(OfferDeliveryReq))
            .Timeout(SampleTimings.OfferRequestTimeout)
            .Async<OfferDeliveryRes>(cancellationToken);
    }
}
