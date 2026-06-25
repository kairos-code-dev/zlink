package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class NoopIngressHandler implements ZLinkRequestHandler<String, String> {
    private final ScenarioState state;

    public NoopIngressHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public String handle(
        String request,
        ZLinkRequestContext context) {
        state.record("IngressRequest", "channel", request);
        return request;
    }
}
