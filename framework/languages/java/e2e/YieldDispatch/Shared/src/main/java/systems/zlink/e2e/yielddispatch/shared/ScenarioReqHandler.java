package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.ZLinkSpotAddress;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ScenarioReqHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ScenarioReq> {
    private static final Duration ROUTE_REQUEST_TIMEOUT = Duration.ofSeconds(30);

    private final ZLinkRouteClient routes;
    private final EvidenceStore evidence;

    public ScenarioReqHandler(
        ZLinkRouteClient routes,
        EvidenceStore evidence) {
        this.routes = routes;
        this.evidence = evidence;
    }

    @Override
    public String packetName() {
        return "ScenarioReq";
    }

    @Override
    public Class<Contracts.ScenarioReq> messageType() {
        return Contracts.ScenarioReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.ScenarioReq request) {
        evidence.record("scenario-started", request.scenarioId(), request.requestId());
        Object spotRequest = switch (request.scenarioId()) {
            case "YD-A1" -> new Contracts.HoldReq(request.requestId());
            case "YD-A2" -> new Contracts.YieldReq(request.scenarioId(), request.requestId(), "corr-a2");
            case "YD-A3" -> new Contracts.YieldReq(request.scenarioId(), request.requestId(), "corr-a3");
            case "YD-A4" -> new Contracts.WorkerYieldReq(request.requestId());
            default -> throw new IllegalArgumentException("unknown scenario " + request.scenarioId());
        };
        RoutingId targetNodeRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE));
        RoutingId targetSpotRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT));
        var scenarioFuture = routes.requestToSpot(
                Contracts.ROUTE_CHANNEL,
                new ZLinkSpotAddress(Contracts.SPOT_MESH, targetNodeRid, targetSpotRid),
                spotRequest)
            .metadata(Contracts.SPOT_RID_METADATA, targetSpotRid.toString())
            .timeout(ROUTE_REQUEST_TIMEOUT)
            .submit(Contracts.ScenarioRes.class)
            .toCompletableFuture();

        if ("YD-A1".equals(request.scenarioId())) {
            sleep(100);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    new ZLinkSpotAddress(
                        Contracts.SPOT_MESH,
                        RoutingId.from(Contracts.PLAY_NODE),
                        RoutingId.from(Contracts.TARGET_SPOT)),
                    new Contracts.ProbeReq(request.requestId()))
                .timeout(ROUTE_REQUEST_TIMEOUT)
                .await(Contracts.ProbeRes.class);
        } else if ("YD-A2".equals(request.scenarioId()) || "YD-A4".equals(request.scenarioId())) {
            sleep(150);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    new ZLinkSpotAddress(Contracts.SPOT_MESH, targetNodeRid, targetSpotRid),
                    new Contracts.ProbeReq(request.requestId()))
                .timeout(ROUTE_REQUEST_TIMEOUT)
                .await(Contracts.ProbeRes.class);
        }

        Contracts.ScenarioRes reply;
        try {
            reply = scenarioFuture.join();
        } catch (RuntimeException error) {
            evidence.record(
                "scenario-failed",
                request.scenarioId(),
                error.getClass().getName() + ":" + error.getMessage());
            error.printStackTrace(System.err);
            throw error;
        }
        context.client().reply(reply).await();
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
    }
}
