package systems.zlink.e2e.discoveryregistryha.embedded;

import java.util.List;
import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record EmbeddedOptions(
    int registryId,
    String registryPubEndpoint,
    String registryRouterEndpoint,
    String httpEndpoint,
    String rid,
    String channelEndpoint,
    List<String> discoveryEndpoints,
    List<String> peerPubEndpoints,
    String evidenceFile,
    String logDir) {
    public static EmbeddedOptions fromEnv() {
        String router = Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER");
        List<String> discovery = Env.csv("ZLINK_JAVA_E2E_REGISTRY_ROUTERS");
        return new EmbeddedOptions(
            Integer.parseInt(Env.get("ZLINK_JAVA_E2E_REGISTRY_ID", "1")),
            Env.get("ZLINK_JAVA_E2E_REGISTRY_PUB"),
            router,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_PROVIDER_RID", "api-a"),
            Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"),
            discovery.isEmpty() ? List.of(router) : discovery,
            Env.csv("ZLINK_JAVA_E2E_REGISTRY_PEERS"),
            Env.get("ZLINK_JAVA_E2E_EVIDENCE_FILE"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
