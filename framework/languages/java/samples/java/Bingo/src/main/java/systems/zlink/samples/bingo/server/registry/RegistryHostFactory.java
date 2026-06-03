package systems.zlink.samples.bingo.server.registry;

import systems.zlink.framework.ZLinkRegistry;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;

public final class RegistryHostFactory {
    private RegistryHostFactory() {
    }

    public static ZLinkRegistry start() {
        return ZLinkRegistry.start(options -> {
            options.setPubEndpoint(SampleTopology.RegistryPubEndpoint);
            options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint);
        });
    }
}
