using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.CustomerGateway;

[ZLinkHandlerGroup(SampleNames.CustomerRouteChannel)]
internal sealed class EnsureCustomerActorHandler(IZLinkActorManager actors)
    : IZLinkRequestHandler<EnsureCustomerActorReq, EnsureCustomerActorRes>
{
    public async ValueTask<EnsureCustomerActorRes> HandleAsync(
        EnsureCustomerActorReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        var actor = await actors.GetOrCreateAsync(
            request.CustomerId,
            SampleNames.CustomerActorType,
            request,
            cancellationToken);

        return new EnsureCustomerActorRes(
            request.CustomerId,
            new ActorRefSnapshot(actor.NodeRid.ToString(), actor.ActorId, actor.Generation));
    }
}

[ZLinkHandlerGroup(SampleNames.CustomerRouteChannel)]
internal sealed class CustomerStatusPushHandler(CustomerActorDirectory customers)
    : IZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes>
{
    public async ValueTask<DeliveryStatusChangedRes> HandleAsync(
        DeliveryStatusChangedReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        await customers.PushAsync(request, cancellationToken);
        return new DeliveryStatusChangedRes(request.DeliveryId, request.Status);
    }
}
