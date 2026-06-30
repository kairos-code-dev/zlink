package systems.zlink.e2e.discoveryregistryha.registry;

import java.util.List;
import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record RegistryOptions(
    int registryId,
    String pubEndpoint,
    String routerEndpoint,
    String httpEndpoint,
    List<String> peerEndpoints) {

    public static RegistryOptions fromEnv() {
        return new RegistryOptions(
            Integer.parseInt(Env.get("ZLINK_JAVA_E2E_REGISTRY_ID", "1")),
            Env.get("ZLINK_JAVA_E2E_REGISTRY_PUB"),
            Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.csv("ZLINK_JAVA_E2E_REGISTRY_PEERS"));
    }
}
