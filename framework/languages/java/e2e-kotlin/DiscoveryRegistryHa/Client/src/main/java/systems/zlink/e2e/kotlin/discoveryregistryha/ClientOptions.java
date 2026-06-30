package systems.zlink.e2e.kotlin.discoveryregistryha;

import java.util.List;

public record ClientOptions(
    String scenario,
    List<String> registryRouters,
    String queryRegistryRouter,
    List<String> probeHttpEndpoints,
    String topologyHttpEndpoint,
    String consumerHttpEndpoint,
    String remoteProbeHttpEndpoint,
    boolean waitForMembers,
    List<String> expectedRids,
    String deadHttpEndpoint,
    String logDir) {
    public static ClientOptions parse(String[] args) {
        CliOptions options = CliOptions.parse(args);
        List<String> registryRouters = options.csv("--registry-routers");
        String queryRegistryRouter = options.get("--query-registry-router");
        if (queryRegistryRouter.isBlank() && !registryRouters.isEmpty()) {
            queryRegistryRouter = registryRouters.get(0);
        }
        return new ClientOptions(
            options.get("--scenario"),
            registryRouters,
            queryRegistryRouter,
            options.csv("--probe-http-endpoints"),
            options.get("--topology-http-endpoint"),
            options.get("--consumer-http-endpoint"),
            options.get("--remote-probe-http-endpoint"),
            Boolean.parseBoolean(options.get("--wait-for-members", "true")),
            options.csv("--expected-rids"),
            options.get("--dead-http-endpoint"),
            options.get("--log-dir", "logs"));
    }
}
