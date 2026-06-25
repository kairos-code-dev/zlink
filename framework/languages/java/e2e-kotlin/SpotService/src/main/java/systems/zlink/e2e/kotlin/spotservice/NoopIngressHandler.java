package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class NoopIngressHandler implements ZLinkRequestHandler<String, String> {
    @Override
    public String handle(
        String request,
        ZLinkRequestContext context) {
        return request;
    }
}
