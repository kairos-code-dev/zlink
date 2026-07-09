package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ShutdownRecoveryReqRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.YieldShutdownRecoveryReq> {
    private static final Duration RECOVERY_DEADLINE = Duration.ofSeconds(90);
    private static final Duration ATTEMPT_TIMEOUT = Duration.ofSeconds(15);

    private final ZLinkRouteClient routes;

    public ShutdownRecoveryReqRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "YieldShutdownRecoveryReq";
    }

    @Override
    public Class<Contracts.YieldShutdownRecoveryReq> messageType() {
        return Contracts.YieldShutdownRecoveryReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.YieldShutdownRecoveryReq request) {
        RoutingId targetNode = SpotMsgRouteHandler.targetNode(dispatch);
        ensureSpot(targetNode, request.spotRid());
        Contracts.EvidenceRes evidence = requestRecoveryProbe(targetNode, request);
        context.client()
            .reply(new Contracts.YieldShutdownRecoveryRes(
                "yield.e3-shutdown-recovery",
                request.spotRid(),
                evidence.markers()))
            .await();
    }

    private void ensureSpot(
        RoutingId targetNode,
        String spotRid) {
        runWithRecoveryDeadline(() -> {
            routes.requestToNode(Contracts.SPOT_MESH, targetNode, new Contracts.EnsureSpotReq(spotRid))
                .timeout(ATTEMPT_TIMEOUT)
                .await(Contracts.EnsureSpotRes.class);
            return null;
        });
    }

    private Contracts.EvidenceRes requestRecoveryProbe(
        RoutingId targetNode,
        Contracts.YieldShutdownRecoveryReq request) {
        return runWithRecoveryDeadline(() -> {
            Contracts.ProbeRes reply = routes
                .requestToSpot(
                    Contracts.SPOT_MESH,
                    new SpotRef(
                        Contracts.SPOT_MESH,
                        targetNode,
                        RoutingId.from(request.spotRid())),
                    new Contracts.ProbeReq("shutdown-recovery-probe", 0))
                .timeout(ATTEMPT_TIMEOUT)
                .await(Contracts.ProbeRes.class);
            return new Contracts.EvidenceRes(
                request.requestId(),
                List.of("probe-completed|spot=" + reply.spotRid()
                    + ";node=" + reply.nodeRid()
                    + ";marker=shutdown-recovery-probe"
                    + ";value=" + reply.value()));
        });
    }

    private static <T> T runWithRecoveryDeadline(RecoveryAttempt<T> attempt) {
        long deadline = System.nanoTime() + RECOVERY_DEADLINE.toNanos();
        RuntimeException last = null;
        while (System.nanoTime() < deadline) {
            try {
                return attempt.run();
            } catch (RuntimeException error) {
                last = error;
                sleep();
            }
        }
        throw new IllegalStateException("YD-E3 recovery did not route to restarted play node", last);
    }

    private static void sleep() {
        try {
            Thread.sleep(500);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("YD-E3 recovery interrupted", error);
        }
    }

    @FunctionalInterface
    private interface RecoveryAttempt<T> {
        T run();
    }
}
