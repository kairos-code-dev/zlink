package systems.zlink.samples.deliverydispatch.server.dispatchapi.handlers;

import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class CreateDeliveryHandler
    implements ZLinkRequestHandler<Messages.CreateDeliveryRequest, Messages.DeliveryCreated> {
    private final ZLinkClient channels;

    public CreateDeliveryHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public Messages.DeliveryCreated handle(
        Messages.CreateDeliveryRequest request,
        ZLinkRequestContext context) {
        Messages.AssignDelivery assign = new Messages.AssignDelivery(
            request.deliveryId(),
            request.customerId(),
            request.pickupAddress(),
            request.dropoffAddress());
        Messages.AssignDeliveryResult assigned = requestDispatch(assign);
        System.err.printf(
            "deliverydispatch api: created delivery=%s courier=%s%n",
            assigned.deliveryId(),
            assigned.courierId());
        return new Messages.DeliveryCreated(assigned.deliveryId());
    }

    private Messages.AssignDeliveryResult requestDispatch(Messages.AssignDelivery request) {
        RuntimeException lastError = null;
        for (int attempt = 1; attempt <= SampleTimings.MaxChannelAttempts; attempt++) {
            try {
                return channels.requestToChannel(SampleNames.DispatchChannel, request)
                    .timeout(SampleTimings.RequestTimeout)
                    .await(Messages.AssignDeliveryResult.class);
            } catch (RuntimeException error) {
                lastError = error;
                pause();
            }
        }
        throw new IllegalStateException(
            "DispatchCenter route was not ready for delivery '" + request.deliveryId() + "'.",
            lastError);
    }

    private static void pause() {
        java.util.concurrent.locks.LockSupport.parkNanos(SampleTimings.ChannelRetryDelay.toNanos());
    }
}
