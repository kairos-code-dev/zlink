package systems.zlink.samples.deliverydispatch.server.courier.handlers;

import java.time.Duration;
import java.util.Locale;
import java.util.concurrent.locks.LockSupport;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.deliverydispatch.server.courier.CourierOptions;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class OfferDeliveryHandler
    implements ZLinkRequestHandler<Messages.OfferDelivery, Messages.OfferDeliveryResult> {
    private final CourierOptions options;

    public OfferDeliveryHandler(CourierOptions options) {
        this.options = options;
    }

    @Override
    public Messages.OfferDeliveryResult handle(
        Messages.OfferDelivery request,
        ZLinkRequestContext context) {
        if (shouldTimeout(request.deliveryId())) {
            System.err.printf(
                "deliverydispatch courier: no response delivery=%s courier=%s%n",
                request.deliveryId(),
                options.courierId());
            LockSupport.parkNanos(Duration.ofSeconds(30).toNanos());
        }
        boolean accepted = !"reject".equalsIgnoreCase(options.mode());
        System.err.printf(
            "deliverydispatch courier: decision delivery=%s courier=%s accepted=%s%n",
            request.deliveryId(),
            options.courierId(),
            accepted);
        return new Messages.OfferDeliveryResult(
            request.deliveryId(),
            options.courierId(),
            accepted,
            accepted ? null : "courier rejected");
    }

    private boolean shouldTimeout(String deliveryId) {
        String mode = options.mode().toLowerCase(Locale.ROOT);
        if ("timeout".equals(mode)) {
            return true;
        }
        return "timeout-reassign".equals(mode)
            && deliveryId.toLowerCase(Locale.ROOT).contains("reassign");
    }
}
