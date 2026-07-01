package systems.zlink.e2e.kotlin.yielddispatch;

import java.util.List;

public final class Contracts {
    public static final String DELAY_CHANNEL = "yield.dispatch.delay";
    public static final String SPOT_MESH = "yield.dispatch.mesh";
    public static final String SPOT_RID_METADATA = "spot-rid";
    public static final String TARGET_NODE_RID_METADATA = "target-node-rid";

    private Contracts() {
    }

    public record DelayReq(String value, long millis) {
    }

    public record DelayRes(String value) {
    }

    public record ScenarioRes(String scenarioId, String requestId, String result) {
    }

    public record ProbeReq(String op, long millis) {
    }

    public record ProbeRes(String spotRid, String nodeRid, String op, String value) {
    }

    public record DispatchReq(String spotRid, String op, long millis) {
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

    public record YieldReq(String scenarioId, String requestId, String correlationId) {
    }

    public record YieldMsg(String requestId, long delayMillis, String correlationId) {
    }

    public record HoldMsg(String requestId, long delayMillis) {
    }

    public record WorkerYieldMsg(String requestId, long delayMillis) {
    }

    public record ProbeMsg(String requestId, String marker) {
    }

    public record RemoteSpotYieldReq(String requestId, String targetSpotRid, long delayMillis) {
    }

    public record YieldTimeoutMsg(String requestId, long delayMillis, long timeoutMillis) {
    }

    public record YieldCancelMsg(String requestId, long delayMillis, long cancelAfterMillis) {
    }

    public record SpotProbeMsg(String requestId, String marker) {
    }

    public record EvidenceReq(String requestId) {
    }

    public record EvidenceRes(String requestId, List<String> markers) {
    }

    public record ActorAuthReq(String actorId) {
    }

    public record ActorAuthRes(String actorId) {
    }

    public record ActorJoinReq(String spotRid, String value, long millis) {
        public ActorJoinReq(String spotRid, String value) {
            this(spotRid, value, 0);
        }
    }

    public record ActorJoinRes(String actorId, String spotRid, String value) {
    }

    public record ActorYieldReq(String requestId, long delayMillis) {
    }

    public record ActorFastReq(String requestId, String marker) {
    }

    public record ActorRes(String scenarioId, String requestId, String actorId, String marker) {
    }

}
