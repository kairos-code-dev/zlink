package systems.zlink.e2e.discoveryregistryha.provider;

import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record ProviderOptions(
    String rid,
    String httpEndpoint,
    String channelEndpoint,
    String redisLocationEndpoint,
    String locationKeyPrefix,
    long redisCommandTimeoutMillis,
    long heartbeatMillis,
    long leaseTtlMillis,
    long pollingMillis,
    long storeFailureGraceMillis,
    String evidenceFile,
    String logDir) {
    public static ProviderOptions fromEnv() {
        String rid = Env.get("ZLINK_JAVA_E2E_PROVIDER_RID", "api-a");
        return new ProviderOptions(
            rid,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_REDIS_COMMAND_TIMEOUT_MS", "5000")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_HEARTBEAT_MS", "1000")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_LEASE_TTL_MS", "3000")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_POLLING_MS", "500")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_STORE_FAILURE_GRACE_MS", "6000")),
            Env.get("ZLINK_JAVA_E2E_EVIDENCE_FILE"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
