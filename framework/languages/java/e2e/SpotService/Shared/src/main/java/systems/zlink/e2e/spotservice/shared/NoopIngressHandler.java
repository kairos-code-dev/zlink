package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class NoopIngressHandler implements ZLinkRequestHandler<Contracts.StateReq, String> {
    private final ScenarioState state;

    public NoopIngressHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public java.util.concurrent.CompletionStage<String> handle(
        Contracts.StateReq request,
        ZLinkRequestContext context) {
        state.record("IngressReq", "channel", request.op());
        return java.util.concurrent.CompletableFuture.completedFuture(request.op());
    }
}
