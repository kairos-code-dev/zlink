package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
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
        Messages.CourierActorEnsured ensured = routes
            .requestTo(
                SampleNames.CourierActorNodeRouteChannel,
                RoutingId.from(directory.choosePlacement(request.courierId())),
                new Messages.EnsureCourierActor(request.courierId()))
            .await(Messages.CourierActorEnsured.class);
        CourierBinding binding = directory.remember(ensured, request.sessionRoute());
        return new Messages.CourierBound(request.courierId(), binding.actor(), binding.sessionRoute());
    }
}
