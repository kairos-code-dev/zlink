package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierBinding;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-gateway")
public final class OfferDeliveryHandler
    implements ZLinkRequestHandler<Messages.OfferDelivery, Messages.OfferDeliveryResult> {
    private final CourierDirectory directory;
    private final ZLinkRouteClient routes;

    public OfferDeliveryHandler(
        CourierDirectory directory,
        ZLinkRouteClient routes) {
        this.directory = directory;
        this.routes = routes;
    }

    @Override
    public Messages.OfferDeliveryResult handle(
        Messages.OfferDelivery request,
        ZLinkRequestContext context) {
        CourierBinding binding = directory.require(request.courierId());
        return routes
            .requestTo(
                SampleNames.CourierActorNodeRouteChannel,
                RoutingId.from(binding.actor().nodeRid()),
                request)
            .timeout(SampleTimings.OfferRequestTimeout)
            .await(Messages.OfferDeliveryResult.class);
    }
}
