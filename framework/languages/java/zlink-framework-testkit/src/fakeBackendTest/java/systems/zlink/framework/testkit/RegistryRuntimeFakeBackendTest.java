package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.service.registry.RegistryState;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryFilter;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient;

final class RegistryRuntimeFakeBackendTest {
    @Test
    void embeddedRegistryBindsAndExposesTypedQuerySurface() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint("tcp://127.0.0.1:5551");
        options.setRouterEndpoint("tcp://127.0.0.1:5552");
        options.setHeartbeatInterval(Duration.ofMillis(250));
        options.setHeartbeatTimeout(Duration.ofSeconds(2));
        options.setBroadcastInterval(Duration.ofMillis(500));
        options.addPeer("tcp://127.0.0.1:5553");

        try (ZLinkRegistryRuntime runtime = RuntimeTestSupport.startRegistry(
                 options,
                 backendFactory,
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))) {
            assertEquals(RegistryState.ACTIVE,
                runtime.status().toCompletableFuture().join().state());
            assertEquals("profile",
                runtime.serviceSummary(ZLinkRegistryServiceSummaryFilter.all())
                    .toCompletableFuture()
                    .join()
                    .get(0)
                    .channelName());
            assertEquals("inproc://profile-server",
                runtime.topology(ZLinkRegistryTopologyFilter.channel("profile"))
                    .toCompletableFuture()
                    .join()
                    .get(0)
                    .endpoint());
            assertEquals("inproc://profile-member",
                runtime.memberPeers("profile")
                    .toCompletableFuture()
                    .join()
                    .get(0)
                    .endpoint());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "factory.registry",
                "create.context",
                "create.registry",
                "registry.setHeartbeat.250.2000",
                "registry.setBroadcastInterval.500",
                "registry.bind.tcp://127.0.0.1:5551.tcp://127.0.0.1:5552",
                "registry.connectPeer.tcp://127.0.0.1:5553",
                "registry.status",
                "registry.serviceSummary.*",
                "registry.topology.profile",
                "registry.memberPeers.profile",
                "close.registry",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void remoteRegistryQueryClientConnectsAndReturnsTypedTopology() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkRemoteRegistryQueryClient client =
                 new ZLinkRemoteRegistryQueryClient(
                     "tcp://127.0.0.1:5552",
                     backendFactory,
                     new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))) {
            assertEquals("tcp://127.0.0.1:7100",
                client.topology(ZLinkRegistryTopologyFilter.channel("profile"))
                    .toCompletableFuture()
                    .join()
                    .get(0)
                    .endpoint());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "factory.registry",
                "create.context",
                "create.registryQueryClient",
                "registryQueryClient.connect.tcp://127.0.0.1:5552",
                "registryQueryClient.topology.profile",
                "close.registryQueryClient",
                "close.context"),
            backendFactory.calls());
    }
}
