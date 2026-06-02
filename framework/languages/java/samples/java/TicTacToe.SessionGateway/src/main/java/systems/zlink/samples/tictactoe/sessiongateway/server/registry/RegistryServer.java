package systems.zlink.samples.tictactoe.sessiongateway.server.registry;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;

public final class RegistryServer {
    private RegistryServer() {
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.useDiscovery(registry -> {
        });
    }
}
