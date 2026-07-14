package systems.zlink.e2e.storefailure.consumer;

import systems.zlink.e2e.storefailure.shared.Env;

public record ConsumerOptions(
    String rid,
    String httpEndpoint,
    String redisLocationEndpoint,
    String locationKeyPrefix,
    long redisCommandTimeoutMillis,
    long heartbeatMillis,
    long leaseTtlMillis,
    long pollingMillis,
    long storeFailureGraceMillis,
    String storeMode,
    String storeDelayControlFile,
    String logDir) {
    public static ConsumerOptions fromEnv() {
        return new ConsumerOptions(
            Env.get("ZLINK_JAVA_E2E_CONSUMER_RID", "consumer"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_REDIS_COMMAND_TIMEOUT_MS", "5000")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_HEARTBEAT_MS", "1000")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_LEASE_TTL_MS", "3000")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_POLLING_MS", "500")),
            Long.parseLong(Env.get("ZLINK_JAVA_E2E_LOCATION_STORE_FAILURE_GRACE_MS", "6000")),
            Env.get("ZLINK_JAVA_E2E_LOCATION_STORE_MODE", "stamp"),
            Env.get("ZLINK_JAVA_E2E_STORE_DELAY_CONTROL_FILE"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
