package systems.zlink.e2e.discoveryregistryha.client.support;

import java.util.List;
import systems.zlink.e2e.discoveryregistryha.shared.Env;

public record ClientOptions(
    String scenario,
    List<String> expectedRids,
    List<String> expectedMemberRids,
    List<String> probeHttpEndpoints,
    String topologyHttpEndpoint,
    String remoteTopologyHttpEndpoint,
    String consumerHttpEndpoint,
    String deadHttpEndpoint) {

    public static ClientOptions fromEnv() {
        List<String> expected = Env.csv("ZLINK_JAVA_E2E_EXPECTED_RIDS");
        List<String> expectedMembers = Env.csv("ZLINK_JAVA_E2E_EXPECTED_MEMBER_RIDS");
        if (expectedMembers.isEmpty()) {
            expectedMembers = expected;
        }
        String topologyEndpoint = Env.get("ZLINK_JAVA_E2E_TOPOLOGY_HTTP_ENDPOINT");
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_SCENARIO"),
            expected,
            expectedMembers,
            Env.csv("ZLINK_JAVA_E2E_PROBE_HTTP_ENDPOINTS"),
            topologyEndpoint,
            Env.get("ZLINK_JAVA_E2E_REMOTE_TOPOLOGY_HTTP_ENDPOINT", topologyEndpoint),
            Env.get("ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_DEAD_HTTP_ENDPOINT"));
    }
}
