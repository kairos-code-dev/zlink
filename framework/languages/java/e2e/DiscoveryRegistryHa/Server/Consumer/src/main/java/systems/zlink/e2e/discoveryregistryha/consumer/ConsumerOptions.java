package systems.zlink.e2e.discoveryregistryha.consumer;

import java.util.List;
import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record ConsumerOptions(
    String rid,
    String httpEndpoint,
    List<String> discoveryEndpoints,
    String logDir) {
    public static ConsumerOptions fromEnv() {
        return new ConsumerOptions(
            Env.get("ZLINK_JAVA_E2E_CONSUMER_RID", "consumer"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.csv("ZLINK_JAVA_E2E_REGISTRY_ROUTERS"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
