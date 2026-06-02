package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.ZLinkBackendAutoConnectType;
import systems.zlink.framework.runtime.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.ZLinkBackendContext;
import systems.zlink.framework.runtime.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.ZLinkBackendDiscovery;
import systems.zlink.framework.runtime.ZLinkBackendSpotNodeMode;
import systems.zlink.framework.runtime.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.ZLinkChannelBackendAdapter;

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
        stream.attachActorGateway(spotNode);

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
                "stream.attachActorGateway.spotNode",
                "factory.registry",
                "create.registry",
                "factory.monitoring",
                "monitoring.open.stream",
                "create.socketMonitor"),
            factory.calls());
    }
}
