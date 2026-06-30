package systems.zlink.e2e.yielddispatch.shared;

import java.util.List;

public final class Contracts {
    public static final String DELAY_CHANNEL = "yield.dispatch.delay";
    public static final String ROUTE_CHANNEL = "yield.dispatch.route";
    public static final String SPOT_MESH = "yield.dispatch.spot";
    public static final String TARGET_SPOT = "yield-probe";
    public static final String PLAY_NODE_A = "play-a";
    public static final String PLAY_NODE_B = "play-b";
    public static final String PLAY_NODE = PLAY_NODE_A;
    public static final String SPOT_RID_METADATA = "spot-rid";
    public static final String TARGET_NODE_RID_METADATA = "target-node-rid";
    public static final String ACTOR_ID_METADATA = "actor-id";
    public static final String ACTOR_TYPE = "yield.actor";

    private Contracts() {
    }

    public record ScenarioRequest(String scenarioId, String requestId) {
    }

    public record ScenarioReply(String scenarioId, String requestId, String result) {
    }

    public record DelayRequest(String requestId, long delayMillis) {
    }

    public record DelayReply(String requestId) {
    }

    public record HoldRequest(String requestId) {
    }

    public record YieldRequest(String scenarioId, String requestId, String correlationId) {
    }

    public record YieldCommand(String requestId, long delayMillis, String correlationId) {
    }

    public record YieldTimeoutCommand(String requestId, long delayMillis, long timeoutMillis) {
    }

    public record YieldShutdownScenarioRequest(String requestId, String spotRid, long delayMillis) {
    }

    public record YieldShutdownRecoveryRequest(String requestId, String spotRid) {
    }

    public record YieldShutdownResult(String operation, String requestId, String spotRid) {
    }

    public record WorkerYieldRequest(String requestId) {
    }

    public record WorkerYieldCommand(String requestId, long delayMillis) {
    }

    public record ProbeRequest(String requestId) {
    }

    public record ProbeCommand(String requestId, String marker) {
    }

    public record TimerStartCommand(
        String requestId,
        String timerName,
        String mode,
        long periodMillis,
        long delayMillis) {
    }

    public record TimerStopCommand(String requestId) {
    }

    public record EnsureSpotRequest(String spotRid) {
    }

    public record EnsureSpotReply(String spotRid, String nodeRid) {
    }

    public record RemoteSpotYieldRequest(String requestId, String targetSpotRid, long delayMillis) {
    }

    public record ProbeReply(String requestId) {
    }

    public record BindActorsRequest(String spotRid, String actorA, String actorB) {
    }

    public record ActorBinding(String actorId, String nodeRid, long epoch) {
    }

    public record BindActorsReply(String spotRid, String actorA, String actorB, List<ActorBinding> actors) {
    }

    public record ActorYieldRequest(String requestId, long delayMillis) {
    }

    public record ActorFastRequest(String requestId, String marker) {
    }

    public record ActorJoinRequest(String requestId, String spotRid) {
    }

    public record ActorJoinYieldRequest(String requestId, String targetSpotRid) {
    }

    public record ActorPushYieldRequest(String requestId, long delayMillis, String value) {
    }

    public record ActorPushNotify(String actorId, String requestId, String value, String nodeRid) {
    }

    public record ActorReply(String scenarioId, String requestId, String actorId, String marker) {
    }

    public record EvidenceEntry(long sequence, String marker, String subject, String value) {
    }

    public record EvidenceSnapshot(String nodeRid, List<EvidenceEntry> entries) {
    }
}
