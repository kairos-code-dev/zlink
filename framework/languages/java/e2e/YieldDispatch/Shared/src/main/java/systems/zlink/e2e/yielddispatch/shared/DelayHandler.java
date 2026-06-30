package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class DelayHandler
    implements ZLinkRequestHandler<Contracts.DelayReq, Contracts.DelayRes> {
    @Override
    public Contracts.DelayRes handle(
        Contracts.DelayReq request,
        ZLinkRequestContext context) {
        try {
            Thread.sleep(request.delayMillis());
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("delay interrupted", error);
        }
        return new Contracts.DelayRes(request.requestId());
    }
}
