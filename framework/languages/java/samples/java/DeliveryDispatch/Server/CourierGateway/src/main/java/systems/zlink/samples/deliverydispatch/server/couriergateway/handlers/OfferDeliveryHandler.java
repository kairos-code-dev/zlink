package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierBinding;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("courier-gateway")
public final class OfferDeliveryHandler
    implements ZLinkRequestHandler<Messages.OfferDeliveryReq, Messages.OfferDeliveryRes> {
    private final CourierDirectory directory;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spotHandles;

    public OfferDeliveryHandler(
        CourierDirectory directory,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles) {
        this.directory = directory;
        this.routes = routes;
        this.spotHandles = spotHandles;
    }

    @Override
    public CompletionStage<Messages.OfferDeliveryRes> handle(
        Messages.OfferDeliveryReq request,
        ZLinkRequestContext context) {
        CourierBinding binding = directory.require(request.courierId());
        return spotHandles.resolveSpotHandle(binding.actor().nodeRid())
            .thenApply(found -> found.orElseThrow(() -> new IllegalStateException(
                "courier Spot is not registered: " + binding.actor().nodeRid())))
            .thenCompose(spot -> routes.requestToSpot(
                    SampleNames.CourierSpotDiscovery,
                    spot,
                    request)
                .timeout(SampleTimings.OfferRequestTimeout)
                .submit(Messages.OfferDeliveryRes.class));
    }
}
