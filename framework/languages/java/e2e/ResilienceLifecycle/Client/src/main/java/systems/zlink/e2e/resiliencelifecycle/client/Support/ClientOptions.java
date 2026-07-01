package systems.zlink.e2e.resiliencelifecycle.client.Support;

import systems.zlink.e2e.resiliencelifecycle.shared.Env;

public record ClientOptions(
    String registryRouterEndpoint,
    String apiAEndpoint,
    String apiBEndpoint,
    String apiAReplacementEndpoint,
    String httpAEndpoint,
    String httpBEndpoint,
    String httpAReplacementEndpoint,
    String buildDir,
    String logDir) {
    public static ClientOptions fromEnv() {
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"),
            Env.get("ZLINK_JAVA_E2E_API_A_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_B_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_A_REPLACEMENT_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_BUILD_DIR",
                System.getProperty("user.home") + "/.cache/zlink/java-e2e/ResilienceLifecycle"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
