using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.CustomerGateway;

[ZLinkHandlerGroup(SampleNames.CustomerRouteChannel)]
internal sealed class EnsureCustomerActorHandler(IZLinkActorManager actors)
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
            new ActorRefSnapshot(actor.NodeRid.ToString(), actor.ActorId, actor.Generation));
    }
}

[ZLinkHandlerGroup(SampleNames.CustomerRouteChannel)]
internal sealed class CustomerStatusPushHandler(CustomerActorDirectory customers)
    : IZLinkRequestHandler<DeliveryStatusChanged, DeliveryStatusAck>
{
    public async ValueTask<DeliveryStatusAck> HandleAsync(
        DeliveryStatusChanged request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await customers.PushAsync(request, cancellationToken);
        return new DeliveryStatusAck(request.DeliveryId, request.Status);
    }
}
