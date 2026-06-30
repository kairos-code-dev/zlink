package systems.zlink.e2e.discoveryregistryha.probe;

import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record ProbeOptions(
    String httpEndpoint,
    String queryRegistryRouter) {

    public static ProbeOptions fromEnv() {
        return new ProbeOptions(
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_QUERY_REGISTRY_ROUTER"));
    }
}
