package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.locations.ZLinkSpotAddress;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierBinding;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-gateway")
public final class BindCourierHandler
    implements ZLinkRequestHandler<Messages.BindCourier, Messages.CourierBound> {
    private final CourierDirectory directory;
    private final ZLinkRouteClient routes;

    public BindCourierHandler(
        CourierDirectory directory,
        ZLinkRouteClient routes) {
        this.directory = directory;
        this.routes = routes;
    }

    @Override
    public Messages.CourierBound handle(
        Messages.BindCourier request,
        ZLinkRequestContext context) {
        String placement = directory.choosePlacement(request.courierId());
        ZLinkSpotAddress address = address(placement);
        Messages.CourierActorFound found = routes
            .requestToSpot(SampleNames.CourierSpotDiscovery, address, new Messages.FindCourierActor(request.courierId()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CourierActorFound.class);
        Messages.CourierActorEnsured ensured = found.actor() == null
            ? routes
                .requestToSpot(
                    SampleNames.CourierSpotDiscovery,
                    address,
                    new Messages.EnsureCourierActor(request.courierId()))
                .timeout(SampleTimings.RequestTimeout)
                .await(Messages.CourierActorEnsured.class)
            : new Messages.CourierActorEnsured(request.courierId(), found.actor());
        CourierBinding binding = directory.remember(ensured, request.sessionRoute());
        return new Messages.CourierBound(request.courierId(), binding.actor(), binding.sessionRoute());
    }

    private static ZLinkSpotAddress address(String placement) {
        RoutingId nodeRid = RoutingId.from(placement);
        return new ZLinkSpotAddress(SampleNames.CourierSpotDiscovery, nodeRid, nodeRid);
    }
}
