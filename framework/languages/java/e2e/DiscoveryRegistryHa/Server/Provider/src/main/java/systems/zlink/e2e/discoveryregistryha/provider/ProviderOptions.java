package systems.zlink.e2e.discoveryregistryha.provider;

import java.util.List;
import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record ProviderOptions(
    String rid,
    String httpEndpoint,
    String channelEndpoint,
    List<String> discoveryEndpoints,
    String evidenceFile,
    String logDir) {
    public static ProviderOptions fromEnv() {
        String rid = Env.get("ZLINK_JAVA_E2E_PROVIDER_RID", "api-a");
        return new ProviderOptions(
            rid,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"),
            Env.csv("ZLINK_JAVA_E2E_REGISTRY_ROUTERS"),
            Env.get("ZLINK_JAVA_E2E_EVIDENCE_FILE"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
