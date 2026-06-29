package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class DelayHandler
    implements ZLinkRequestHandler<Contracts.DelayRequest, Contracts.DelayReply> {
    @Override
    public Contracts.DelayReply handle(
        Contracts.DelayRequest request,
        ZLinkRequestContext context) {
        try {
            Thread.sleep(request.delayMillis());
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("delay interrupted", error);
        }
        return new Contracts.DelayReply(request.requestId());
    }
}
