package systems.zlink.e2e.kotlin.discoveryregistryha;

import java.util.List;

public record RegistryOptions(
    int registryId,
    String pubEndpoint,
    String routerEndpoint,
    String httpEndpoint,
    List<String> peers) {
    public static RegistryOptions parse(String[] args) {
        CliOptions options = CliOptions.parse(args);
        return new RegistryOptions(
            Integer.parseInt(options.get("--registry-id", "1")),
            options.get("--registry-pub"),
            options.get("--registry-router"),
            options.get("--http-endpoint"),
            options.csv("--registry-peers"));
    }
}
