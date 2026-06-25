package systems.zlink.e2e.kotlin.discoveryregistryha;

public final class Contracts {
    public static final String CHANNEL = "discovery.registry.ha.api";
    public static final String HANDLER_GROUP = "discovery-registry-ha";

    private Contracts() {
    }

    public record WorkRequest(String value) {
    }

    public record WorkReply(
        String value,
        String providerRid) {
    }
}
