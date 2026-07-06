package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierBinding;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-gateway")
public final class BindCourierHandler
    implements ZLinkRequestHandler<Messages.BindCourier, Messages.CourierBound> {
    private final CourierDirectory directory;
    private final ZLinkClient channels;

    public BindCourierHandler(
        CourierDirectory directory,
        ZLinkClient channels) {
        this.directory = directory;
        this.channels = channels;
    }

    @Override
    public Messages.CourierBound handle(
        Messages.BindCourier request,
        ZLinkRequestContext context) {
        String placement = directory.choosePlacement(request.courierId());
        Messages.CourierActorEnsured ensured = channels
            .requestToChannel(
                SampleNames.courierActorNodeChannelFor(placement),
                new Messages.EnsureCourierActor(request.courierId()))
            .await(Messages.CourierActorEnsured.class);
        CourierBinding binding = directory.remember(ensured, request.sessionRoute());
        return new Messages.CourierBound(request.courierId(), binding.actor(), binding.sessionRoute());
    }
}
