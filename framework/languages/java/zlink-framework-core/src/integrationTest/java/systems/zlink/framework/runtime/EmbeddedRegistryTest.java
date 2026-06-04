package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryFilter;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;

final class EmbeddedRegistryTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(30_000 + (int) (ProcessHandle.current().pid() % 10_000));

    @Test
    void embeddedRegistry_queryService_resolvesAndReadsStatus() {
        Zlink.version();
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setRegistryId(7);
        String pubEndpoint = tcpEndpoint();
        String routerEndpoint = tcpEndpoint();
        options.setPubEndpoint(pubEndpoint);
        options.setRouterEndpoint(routerEndpoint);

        try (ZLinkRegistryRuntime runtime = new ZLinkRegistryRuntime(
                 options,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))) {
            var status = runtime.statusAsync()
                .toCompletableFuture()
                .join();
            assertEquals(7, status.registryId());
            assertEquals(routerEndpoint, status.bindEndpoint());
            assertTrue(status.topologyEntryCount() >= 0);
            assertTrue(runtime.serviceSummaryAsync(ZLinkRegistryServiceSummaryFilter.all())
                .toCompletableFuture()
                .join()
                .isEmpty());
            assertTrue(runtime.topologyAsync(null)
                .toCompletableFuture()
                .join()
                .isEmpty());
        }
    }

    @Test
    void remoteRegistryQueryClient_canReadTopologySnapshot() {
        Zlink.version();
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(tcpEndpoint());
        String routerEndpoint = tcpEndpoint();
        options.setRouterEndpoint(routerEndpoint);

        try (ZLinkRegistryRuntime ignored = new ZLinkRegistryRuntime(
                 options,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkRemoteRegistryQueryClient client =
                 new ZLinkRemoteRegistryQueryClient(
                     routerEndpoint,
                     new ZLinkJavaBackendAdapterFactory(),
                     new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)))) {
            assertTrue(client.topologyAsync(ZLinkRegistryTopologyFilter.channel("profile"))
                .toCompletableFuture()
                .join()
                .isEmpty());
        }
    }

    private static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + nextPort();
    }

    private static int nextPort() {
        for (int attempt = 0; attempt < 200; attempt++) {
            int port = NEXT_PORT.getAndIncrement();
            if (isBindable(port)) {
                return port;
            }
        }
        throw new IllegalStateException("failed to allocate tcp port");
    }

    private static boolean isBindable(int port) {
        try (ServerSocket server = new ServerSocket(port)) {
            server.setReuseAddress(false);
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }
}
