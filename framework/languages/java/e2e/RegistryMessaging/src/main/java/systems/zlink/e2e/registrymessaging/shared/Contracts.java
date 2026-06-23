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
}
