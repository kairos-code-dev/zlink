package systems.zlink.samples.shoppingmall.server.commerceapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

/**
 * Self-check hook that seeds a pending idempotency mapping so the
 * pending-recovery scenario can prove forwarding to the owning instance.
 */
@ZLinkHandlerGroup("commerce")
public final class CreatePendingMappingHandler
    implements ZLinkRequestHandler<Messages.CreatePendingMappingReq, Messages.CreatePendingMappingRes> {
    private final CommerceStore store;

    public CreatePendingMappingHandler(CommerceStore store) {
        this.store = store;
    }

    @Override
    public Messages.CreatePendingMappingRes handle(
        Messages.CreatePendingMappingReq request,
        ZLinkRequestContext context) {
        store.createPendingMapping(
            request.idempotencyKey(), request.orderId(), request.ownerInstanceId());
        return new Messages.CreatePendingMappingRes(true);
    }
}
