package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.backend.ZLinkBackendAutoConnectType;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendDiscovery;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;

final class FakeZLinkBackendAdapterTest {
    @Test
    void fakeBackendMirrorsSubsystemAdapterBoundaryWithoutBindingConcreteTypes() {
        FakeZLinkBackendAdapterFactory factory = new FakeZLinkBackendAdapterFactory();
        ZLinkBackendAdapterOptions options = new ZLinkBackendAdapterOptions(Duration.ofSeconds(1));
        ZLinkChannelBackendAdapter channel = factory.createChannelAdapter(options);
        ZLinkBackendContext context = channel.createContext();
        ZLinkBackendDiscovery discovery =
            channel.createDiscovery(context, ZLinkBackendAutoConnectType.CLIENT_SERVER, "profile");
        ZLinkBackendDealerSocket dealer = channel.createDealerSocket(context);
        dealer.attachDiscovery(discovery);

        ZLinkBackendSpotNode spotNode =
            factory.createSpotAdapter(options).createSpotNode(context, ZLinkBackendSpotNodeMode.ALL);
        ZLinkBackendStreamSocket stream =
            factory.createStreamAdapter(options).createStreamSocket(context);

        factory.createRegistryAdapter(options).createRegistry(context);
        factory.createMonitoringAdapter(options).openSocketMonitor(stream);

        assertEquals(
            java.util.List.of(
                "factory.channel",
                "create.context",
                "create.discovery.profile",
                "create.dealer",
                "dealer.attachDiscovery.discovery.profile",
                "factory.spot",
                "create.spotNode",
                "factory.stream",
                "create.stream",
                "factory.registry",
                "create.registry",
                "factory.monitoring",
                "monitoring.open.stream",
                "create.socketMonitor"),
            factory.calls());
    }
}
