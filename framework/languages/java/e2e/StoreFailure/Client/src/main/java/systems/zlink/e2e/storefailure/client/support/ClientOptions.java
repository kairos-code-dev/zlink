package systems.zlink.e2e.storefailure.client.support;

import java.util.List;
import systems.zlink.e2e.storefailure.shared.Env;

public record ClientOptions(
    String scenario,
    List<String> expectedRids,
    String consumerHttpEndpoint,
    String deadRid,
    List<String> expectedAbsentRids,
    long locationHeartbeatMillis,
    long locationLeaseTtlMillis,
    long locationPollingMillis,
    long locationStoreFailureGraceMillis) {

    public static ClientOptions fromEnv() {
        List<String> expected = Env.csv("ZLINK_JAVA_E2E_EXPECTED_RIDS");
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_SCENARIO"),
            expected,
            Env.get("ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_DEAD_RID", "api-b"),
            Env.csv("ZLINK_JAVA_E2E_EXPECTED_ABSENT_RIDS"),
            Env.longValue("ZLINK_JAVA_E2E_LOCATION_HEARTBEAT_MS", 1000),
            Env.longValue("ZLINK_JAVA_E2E_LOCATION_LEASE_TTL_MS", 3000),
            Env.longValue("ZLINK_JAVA_E2E_LOCATION_POLLING_MS", 500),
            Env.longValue("ZLINK_JAVA_E2E_LOCATION_STORE_FAILURE_GRACE_MS", 6000));
    }
}
