package systems.zlink.e2e.kotlin.discoveryregistryha;

import java.util.List;

public record ProviderOptions(
    String providerRid,
    String apiEndpoint,
    List<String> registryRouters,
    String logDir) {
    public static ProviderOptions parse(String[] args) {
        CliOptions options = CliOptions.parse(args);
        return new ProviderOptions(
            options.get("--provider-rid", "api-a"),
            options.get("--api-endpoint"),
            options.csv("--registry-routers"),
            options.get("--log-dir", "logs"));
    }
}
