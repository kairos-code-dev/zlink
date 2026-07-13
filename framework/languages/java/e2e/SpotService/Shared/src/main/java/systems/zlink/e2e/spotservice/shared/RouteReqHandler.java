package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;

public final class RouteReqHandler
    implements ZLinkRouteRequestHandler<Contracts.RouteReq, Contracts.RouteRes> {
    private final ScenarioState state;

    public RouteReqHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public java.util.concurrent.CompletionStage<Contracts.RouteRes> handle(
        Contracts.RouteReq request,
        ZLinkRouteRequestContext context) {
        state.record("RouteReq", context.routingId().toString(), request.value());
        return java.util.concurrent.CompletableFuture.completedFuture(new Contracts.RouteRes(
            "route:" + request.value(),
            state.nodeRid(),
            context.routingId().toString()));
    }
}
