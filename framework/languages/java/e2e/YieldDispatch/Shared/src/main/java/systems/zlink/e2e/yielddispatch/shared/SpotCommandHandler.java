package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
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
                new SpotRef(
                    Contracts.SPOT_MESH,
                    targetNodeRid,
                    targetSpotRid),
                command)
            .await();
    }

    public static final class WorkerYield
        extends SpotCommandHandler<Contracts.WorkerYieldMsg> {
        public WorkerYield(ZLinkRouteClient routes) {
            super(routes, Contracts.WorkerYieldMsg.class);
        }

        @Override
        public String packetName() {
            return "WorkerYieldMsg";
        }
    }

    public static final class Yield
        extends SpotCommandHandler<Contracts.YieldMsg> {
        public Yield(ZLinkRouteClient routes) {
            super(routes, Contracts.YieldMsg.class);
        }

        @Override
        public String packetName() {
            return "YieldMsg";
        }
    }

    public static final class Probe
        extends SpotCommandHandler<Contracts.ProbeMsg> {
        public Probe(ZLinkRouteClient routes) {
            super(routes, Contracts.ProbeMsg.class);
        }

        @Override
        public String packetName() {
            return "ProbeMsg";
        }
    }

    public static final class YieldTimeout
        extends SpotCommandHandler<Contracts.YieldTimeoutMsg> {
        public YieldTimeout(ZLinkRouteClient routes) {
            super(routes, Contracts.YieldTimeoutMsg.class);
        }

        @Override
        public String packetName() {
            return "YieldTimeoutMsg";
        }
    }

    public static final class YieldCancel
        extends SpotCommandHandler<Contracts.YieldCancelMsg> {
        public YieldCancel(ZLinkRouteClient routes) {
            super(routes, Contracts.YieldCancelMsg.class);
        }

        @Override
        public String packetName() {
            return "YieldCancelMsg";
        }
    }

    public static final class TimerStart
        extends SpotCommandHandler<Contracts.TimerStartMsg> {
        public TimerStart(ZLinkRouteClient routes) {
            super(routes, Contracts.TimerStartMsg.class);
        }

        @Override
        public String packetName() {
            return "TimerStartMsg";
        }
    }

    public static final class TimerStop
        extends SpotCommandHandler<Contracts.TimerStopMsg> {
        public TimerStop(ZLinkRouteClient routes) {
            super(routes, Contracts.TimerStopMsg.class);
        }

        @Override
        public String packetName() {
            return "TimerStopMsg";
        }
    }
}
