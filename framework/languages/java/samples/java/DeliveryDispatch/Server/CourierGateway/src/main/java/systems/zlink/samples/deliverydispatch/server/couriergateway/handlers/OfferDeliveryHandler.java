package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkClient;
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
    private final ZLinkClient channels;

    public OfferDeliveryHandler(
        CourierDirectory directory,
        ZLinkClient channels) {
        this.directory = directory;
        this.channels = channels;
    }

    @Override
    public Messages.OfferDeliveryResult handle(
        Messages.OfferDelivery request,
        ZLinkRequestContext context) {
        CourierBinding binding = directory.require(request.courierId());
        return channels
            .requestToChannel(
                SampleNames.courierActorNodeChannelFor(binding.actor().nodeRid().toString()),
                request)
            .timeout(SampleTimings.OfferRequestTimeout)
            .await(Messages.OfferDeliveryResult.class);
    }
}
