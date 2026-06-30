package systems.zlink.e2e.kotlin.yielddispatch;

import java.util.List;

public final class Contracts {
    public static final String DELAY_CHANNEL = "yield.dispatch.delay";
    public static final String SPOT_MESH = "yield.dispatch.mesh";
    public static final String SPOT_RID_METADATA = "spot-rid";
    public static final String TARGET_NODE_RID_METADATA = "target-node-rid";

    private Contracts() {
    }

    public record DelayRequest(String value, long millis) {
    }

    public record DelayReply(String value) {
    }

    public record ScenarioReply(String scenarioId, String requestId, String result) {
    }

    public record ProbeRequest(String op, long millis) {
    }

    public record ProbeReply(String spotRid, String nodeRid, String op, String value) {
    }

    public record DispatchRequest(String spotRid, String op, long millis) {
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

    public record YieldRequest(String scenarioId, String requestId, String correlationId) {
    }

    public record YieldCommand(String requestId, long delayMillis, String correlationId) {
    }

    public record HoldCommand(String requestId, long delayMillis) {
    }

    public record WorkerYieldCommand(String requestId, long delayMillis) {
    }

    public record ProbeCommand(String requestId, String marker) {
    }

    public record RemoteSpotYieldRequest(String requestId, String targetSpotRid, long delayMillis) {
    }

    public record YieldTimeoutCommand(String requestId, long delayMillis, long timeoutMillis) {
    }

    public record SpotProbeCommand(String requestId, String marker) {
    }

    public record EvidenceRequest(String requestId) {
    }

    public record EvidenceReply(String requestId, List<String> markers) {
    }

    public record ActorAuthRequest(String actorId) {
    }

    public record ActorAuthReply(String actorId) {
    }

    public record ActorJoinRequest(String spotRid, String value, long millis) {
        public ActorJoinRequest(String spotRid, String value) {
            this(spotRid, value, 0);
        }
    }

    public record ActorJoinReply(String actorId, String spotRid, String value) {
    }

    public record ActorYieldRequest(String requestId, long delayMillis) {
    }

    public record ActorFastRequest(String requestId, String marker) {
    }

    public record ActorReply(String scenarioId, String requestId, String actorId, String marker) {
    }

}
