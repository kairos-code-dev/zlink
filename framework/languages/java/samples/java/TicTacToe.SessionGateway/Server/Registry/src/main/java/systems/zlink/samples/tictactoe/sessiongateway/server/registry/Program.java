package systems.zlink.samples.tictactoe.sessiongateway.server.registry;

import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.ZLinkRegistry;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkRegistry ignored = RegistryHostFactory.start()) {
            new CountDownLatch(1).await();
        }
    }
}

final class RegistryHostFactory {
    private RegistryHostFactory() {
    }

    static ZLinkRegistry start() {
        return ZLinkRegistry.start(options -> {
            options.setPubEndpoint(SampleTopology.RegistryPubEndpoint);
            options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint);
        });
    }
}
