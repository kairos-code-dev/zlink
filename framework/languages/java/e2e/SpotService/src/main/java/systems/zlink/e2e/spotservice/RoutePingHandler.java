package systems.zlink.e2e.spotservice;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;

public final class RoutePingHandler
    implements ZLinkRouteRequestHandler<Contracts.RoutePing, Contracts.RoutePong> {
    private final ScenarioState state;

    public RoutePingHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public Contracts.RoutePong handle(
        Contracts.RoutePing request,
        ZLinkRouteRequestContext context) {
        state.record("RoutePing", context.routingId().toString(), request.value());
        return new Contracts.RoutePong(
            "route:" + request.value(),
            state.nodeRid(),
            context.routingId().toString());
    }
}
