package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ScenarioRequestHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ScenarioRequest> {
    private static final Duration ROUTE_REQUEST_TIMEOUT = Duration.ofSeconds(30);

    private final ZLinkRouteClient routes;
    private final EvidenceStore evidence;

    public ScenarioRequestHandler(
        ZLinkRouteClient routes,
        EvidenceStore evidence) {
        this.routes = routes;
        this.evidence = evidence;
    }

    @Override
    public String packetName() {
        return "ScenarioRequest";
    }

    @Override
    public Class<Contracts.ScenarioRequest> messageType() {
        return Contracts.ScenarioRequest.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.ScenarioRequest request) {
        evidence.record("scenario-started", request.scenarioId(), request.requestId());
        Object spotRequest = switch (request.scenarioId()) {
            case "YD-A1" -> new Contracts.HoldRequest(request.requestId());
            case "YD-A2" -> new Contracts.YieldRequest(request.scenarioId(), request.requestId(), "corr-a2");
            case "YD-A3" -> new Contracts.YieldRequest(request.scenarioId(), request.requestId(), "corr-a3");
            case "YD-A4" -> new Contracts.WorkerYieldRequest(request.requestId());
            default -> throw new IllegalArgumentException("unknown scenario " + request.scenarioId());
        };
        RoutingId targetNodeRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE));
        RoutingId targetSpotRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT));
        var scenarioFuture = routes.requestToSpot(
                Contracts.ROUTE_CHANNEL,
                targetNodeRid,
                targetSpotRid,
                spotRequest)
            .metadata(Contracts.SPOT_RID_METADATA, targetSpotRid.toString())
            .timeout(ROUTE_REQUEST_TIMEOUT)
            .submit(Contracts.ScenarioReply.class)
            .toCompletableFuture();

        if ("YD-A1".equals(request.scenarioId())) {
            sleep(100);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    RoutingId.from(Contracts.PLAY_NODE),
                    RoutingId.from(Contracts.TARGET_SPOT),
                    new Contracts.ProbeRequest(request.requestId()))
                .timeout(ROUTE_REQUEST_TIMEOUT)
                .await(Contracts.ProbeReply.class);
        } else if ("YD-A2".equals(request.scenarioId()) || "YD-A4".equals(request.scenarioId())) {
            sleep(150);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    targetNodeRid,
                    targetSpotRid,
                    new Contracts.ProbeRequest(request.requestId()))
                .timeout(ROUTE_REQUEST_TIMEOUT)
                .await(Contracts.ProbeReply.class);
        }

        Contracts.ScenarioReply reply = scenarioFuture.join();
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
