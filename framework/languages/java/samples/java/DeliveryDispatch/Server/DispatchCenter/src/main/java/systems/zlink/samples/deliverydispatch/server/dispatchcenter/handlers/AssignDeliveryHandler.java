package systems.zlink.samples.deliverydispatch.server.dispatchcenter.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.dispatchcenter.DispatchWorkQueue;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("dispatch")
public final class AssignDeliveryHandler
    implements ZLinkRequestHandler<Messages.AssignDelivery, Messages.AssignDeliveryRes> {
    private final DispatchWorkQueue queue;

    public AssignDeliveryHandler(DispatchWorkQueue queue) {
        this.queue = queue;
    }

    @Override
    public Messages.AssignDeliveryRes handle(
        Messages.AssignDelivery request,
        ZLinkRequestContext context) {
        queue.enqueue(request);
        System.err.printf(
            "deliverydispatch dispatch: queued delivery=%s customer=%s%n",
            request.deliveryId(),
            request.customerId());
        return new Messages.AssignDeliveryRes(request.deliveryId(), "pending");
    }
}
