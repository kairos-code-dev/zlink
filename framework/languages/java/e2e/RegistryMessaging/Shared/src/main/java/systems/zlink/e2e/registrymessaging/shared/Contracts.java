package systems.zlink.e2e.registrymessaging.shared;

import java.util.List;

public final class Contracts {
    public static final String API_CHANNEL = "registry.messaging.api";
    public static final String WORKFLOW_CHANNEL = "registry.messaging.workflow";
    public static final String ROUTE_CHANNEL = "registry.messaging.route";
    public static final String HANDLER_GROUP = "registry-messaging";
    public static final String ROUTE_PACKET = "ScenarioRoutePing";

    private Contracts() {
    }

    public record ProfileRequest(String value) {
    }

    public record ProfileReply(
        String value,
        String providerRid,
        String instanceId) {
    }

    public record ProfileCommand(String commandId) {
    }

    public record PayloadRequest(String marker, String payload) {
    }

    public record PayloadReply(
        String marker,
        int length,
        String sha256) {
    }

    public record WorkflowRequest(String value) {
    }

    public record WorkflowReply(
        String value,
        String providerRid) {
    }

    public record RoutePing(String value) {
    }

    public record RoutePong(
        String value,
        String targetRid,
        String sourceRid) {
    }

    public record EvidenceEntry(
        String marker,
        String providerRid,
        String value) {
    }

    public record EvidenceSnapshot(
        String providerRid,
        List<EvidenceEntry> entries) {
    }

    public record EvidenceWaitRequest(
        String contains,
        int timeoutMilliseconds) {
        public EvidenceWaitRequest(String contains) {
            this(contains, 10000);
        }
    }

    public record RouteMissingResult(boolean failed) {
    }

    public record RequestFailureResult(
        boolean failed,
        String failureType) {
    }

    public record BackpressureResult(String outcome) {
    }
}
