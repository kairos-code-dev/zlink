package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

abstract class SpotMsgRouteHandler<TCommand>
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, TCommand> {
    private final ZLinkRouteClient routes;
    private final Class<TCommand> messageType;

    SpotMsgRouteHandler(
        ZLinkRouteClient routes,
        Class<TCommand> messageType) {
        this.routes = routes;
        this.messageType = messageType;
    }

    @Override
    public Class<TCommand> messageType() {
        return messageType;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        TCommand command) {
        routes.sendToSpot(
                Contracts.SPOT_MESH,
                targetNode(dispatch),
                targetSpot(dispatch),
                command)
            .await();
    }

    static RoutingId targetNode(ZLinkSessionDispatchContext dispatch) {
        return RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.TARGET_NODE_RID_METADATA, "play-a"));
    }

    static RoutingId targetSpot(ZLinkSessionDispatchContext dispatch) {
        return RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.SPOT_RID_METADATA, "room-a"));
    }
}
