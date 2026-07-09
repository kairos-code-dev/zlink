package systems.zlink.e2e.resiliencelifecycle.client.Support;

import systems.zlink.e2e.resiliencelifecycle.shared.Env;

public record ClientOptions(
    String redisLocationEndpoint,
    String locationKeyPrefix,
    String apiAEndpoint,
    String apiBEndpoint,
    String apiAReplacementEndpoint,
    String apiBGreenEndpoint,
    String httpAEndpoint,
    String httpBEndpoint,
    String httpAReplacementEndpoint,
    String httpBGreenEndpoint,
    String storePauseCommand,
    String storeResumeCommand,
    String buildDir,
    String logDir) {
    public static ClientOptions fromEnv() {
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"),
            Env.get("ZLINK_JAVA_E2E_API_A_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_B_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_B_GREEN_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_A_REPLACEMENT_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_B_GREEN_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_STORE_PAUSE_COMMAND"),
            Env.get("ZLINK_JAVA_E2E_STORE_RESUME_COMMAND"),
            Env.get("ZLINK_JAVA_E2E_BUILD_DIR",
                System.getProperty("user.home") + "/.cache/zlink/java-e2e/ResilienceLifecycle"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
