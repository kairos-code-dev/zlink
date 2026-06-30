package systems.zlink.e2e.discoveryregistryha.shared;

public final class Contracts {
    public static final String CHANNEL = "discovery.registry.ha.profile";
    public static final String HANDLER_GROUP = "discovery-registry-ha";

    private Contracts() {
    }

    public record ProfileRequest(
        String value,
        String marker) {
    }

    public record ProfileReply(
        String value,
        String providerRid,
        String marker) {
    }

    public record EvidenceWaitRequest(
        String contains,
        int timeoutMilliseconds) {
    }

    public record TopologyReadyWaitRequest(
        int readyCount,
        int timeoutMilliseconds) {
    }

    public record MemberEndpointWaitRequest(
        String endpoint,
        int timeoutMilliseconds) {
    }
}
