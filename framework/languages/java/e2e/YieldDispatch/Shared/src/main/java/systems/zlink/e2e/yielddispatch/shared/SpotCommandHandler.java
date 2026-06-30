package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public abstract class SpotCommandHandler<TCommand>
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, TCommand> {
    private final ZLinkRouteClient routes;
    private final Class<TCommand> messageType;

    protected SpotCommandHandler(
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
        RoutingId targetNodeRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE));
        RoutingId targetSpotRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT));
        routes.sendToSpot(
                Contracts.ROUTE_CHANNEL,
                targetNodeRid,
                targetSpotRid,
                command)
            .await();
    }

    public static final class WorkerYield
        extends SpotCommandHandler<Contracts.WorkerYieldCommand> {
        public WorkerYield(ZLinkRouteClient routes) {
            super(routes, Contracts.WorkerYieldCommand.class);
        }

        @Override
        public String packetName() {
            return "WorkerYieldCommand";
        }
    }

    public static final class Yield
        extends SpotCommandHandler<Contracts.YieldCommand> {
        public Yield(ZLinkRouteClient routes) {
            super(routes, Contracts.YieldCommand.class);
        }

        @Override
        public String packetName() {
            return "YieldCommand";
        }
    }

    public static final class Probe
        extends SpotCommandHandler<Contracts.ProbeCommand> {
        public Probe(ZLinkRouteClient routes) {
            super(routes, Contracts.ProbeCommand.class);
        }

        @Override
        public String packetName() {
            return "ProbeCommand";
        }
    }

    public static final class YieldTimeout
        extends SpotCommandHandler<Contracts.YieldTimeoutCommand> {
        public YieldTimeout(ZLinkRouteClient routes) {
            super(routes, Contracts.YieldTimeoutCommand.class);
        }

        @Override
        public String packetName() {
            return "YieldTimeoutCommand";
        }
    }

    public static final class TimerStart
        extends SpotCommandHandler<Contracts.TimerStartCommand> {
        public TimerStart(ZLinkRouteClient routes) {
            super(routes, Contracts.TimerStartCommand.class);
        }

        @Override
        public String packetName() {
            return "TimerStartCommand";
        }
    }

    public static final class TimerStop
        extends SpotCommandHandler<Contracts.TimerStopCommand> {
        public TimerStop(ZLinkRouteClient routes) {
            super(routes, Contracts.TimerStopCommand.class);
        }

        @Override
        public String packetName() {
            return "TimerStopCommand";
        }
    }
}
