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

    public record ScenarioReq(String scenarioId, String requestId) {
    }

    public record ScenarioRes(String scenarioId, String requestId, String result) {
    }

    public record DelayReq(String requestId, long delayMillis) {
    }

    public record DelayRes(String requestId) {
    }

    public record HoldReq(String requestId) {
    }

    public record YieldReq(String scenarioId, String requestId, String correlationId) {
    }

    public record YieldMsg(String requestId, long delayMillis, String correlationId) {
    }

    public record YieldTimeoutMsg(String requestId, long delayMillis, long timeoutMillis) {
    }

    public record YieldCancelMsg(String requestId, long delayMillis, long cancelAfterMillis) {
    }

    public record YieldShutdownScenarioReq(String requestId, String spotRid, long delayMillis) {
    }

    public record YieldShutdownRecoveryReq(String requestId, String spotRid) {
    }

    public record YieldShutdownRes(String operation, String requestId, String spotRid) {
    }

    public record WorkerYieldReq(String requestId) {
    }

    public record WorkerYieldMsg(String requestId, long delayMillis) {
    }

    public record ProbeReq(String requestId) {
    }

    public record ProbeMsg(String requestId, String marker) {
    }

    public record TimerStartMsg(
        String requestId,
        String timerName,
        String mode,
        long periodMillis,
        long delayMillis) {
    }

    public record TimerStopMsg(String requestId) {
    }

    public record EnsureSpotReq(String spotRid) {
    }

    public record EnsureSpotRes(String spotRid, String nodeRid) {
    }

    public record RemoteSpotYieldReq(String requestId, String targetSpotRid, long delayMillis) {
    }

    public record ProbeRes(String requestId) {
    }

    public record BindActorsReq(String spotRid, String actorA, String actorB) {
    }

    public record ActorBinding(String actorId, String nodeRid, long epoch) {
    }

    public record BindActorsRes(String spotRid, String actorA, String actorB, List<ActorBinding> actors) {
    }

    public record ActorYieldReq(String requestId, long delayMillis) {
    }

    public record ActorFastReq(String requestId, String marker) {
    }

    public record ActorJoinReq(String requestId, String spotRid) {
    }

    public record ActorJoinYieldReq(String requestId, String targetSpotRid) {
    }

    public record ActorPushYieldReq(String requestId, long delayMillis, String value) {
    }

    public record ActorPushNotify(String actorId, String requestId, String value, String nodeRid) {
    }

    public record ActorYieldRes(String scenarioId, String requestId, String actorId, String marker) {
    }

    public record ActorFastRes(String scenarioId, String requestId, String actorId, String marker) {
    }

    public record ActorJoinRes(String scenarioId, String requestId, String actorId, String marker) {
    }

    public record ActorJoinYieldRes(String scenarioId, String requestId, String actorId, String marker) {
    }

    public record ActorPushYieldRes(String scenarioId, String requestId, String actorId, String marker) {
    }

    public record EvidenceEntry(long sequence, String marker, String subject, String value) {
    }

    public record EvidenceSnapshot(String nodeRid, List<EvidenceEntry> entries) {
    }
}
